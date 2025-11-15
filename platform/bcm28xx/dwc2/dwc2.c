#ifdef ARCH_ARM64
#include <arch/arm64/mmu.h>
#include <kernel/vm.h>
#endif
#include <assert.h>
#include <dev/gpio.h>
#include <host/hcd.h>
#include <kernel/event.h>
#include <kernel/mutex.h>
#include <kernel/timer.h>
#include <lib/bio.h>
#include <lib/heap.h>
#include <lib/hexdump.h>
#include <lk/console_cmd.h>
#include <lk/err.h>
//#include <cksum-helper/cksum-helper.h>
#include <lk/init.h>
#include <lk/list.h>
#include <lk/reg.h>
#include <platform/bcm28xx/clock.h>
#include <platform/bcm28xx/cm.h>
#include <platform/bcm28xx/dwc2.h>
#include <platform/bcm28xx/gpio.h>
#include <platform/bcm28xx/pll.h>
#include <platform/bcm28xx/print_timestamp.h>
#include <platform/bcm28xx/queue.h>
#include <platform/bcm28xx/udelay.h>
#include <platform/bcm28xx/usb.h>
#include <platform/interrupts.h>
#include <platform/time.h>
#include <stdio.h>
#include <stdlib.h>
#include <tusb.h>
#include <usb_utils.h>

#define logf(fmt, ...) { print_timestamp(); printf("[DWC2:%s:%d]: "fmt, __FUNCTION__, __LINE__, ##__VA_ARGS__); }
#define BIT(b) (1 << b)
#define dumpreg(reg) { t = *REG32(reg); printf(#reg":\t 0x%x\n", t); }

#define CSI "\x1b["
#define RED     CSI"31m"
#define GREEN   CSI"32m"
#define DEFAULT CSI"39m"

#define DMA 1

struct dwc2_host_channel {
  volatile uint32_t hcchar;
  volatile uint32_t hcsplt;
  volatile uint32_t hcint;
  volatile uint32_t hcintmsk;

  volatile uint32_t hctsiz;
  volatile uint32_t hcdma;
};

typedef struct {
  struct list_node node;
  int channel;
} idle_channel_t;

typedef struct {
  struct list_node l;
  uint8_t dev_addr;
  uint8_t ep_addr;
  hcd_devtree_info_t info;
  uint8_t next_pid;
  uint8_t ep_type;
  timer_t retry;
  void *buffer;
  int buflen;
  uint32_t max_packet_size;
  int packets;
  idle_channel_t *ic;
} open_endpoint_t;

typedef struct {
  uint32_t *buffer;
  uint32_t buffer_size;
  uint32_t req_start;
  open_endpoint_t *opep;
} channelState;

#define MAX_DEVICES 10
#define MAX_CHANNELS 16

// there is a race condition with 2 threads trying to init the same channel
// for now, just have one mutex for all channel setup
// for later, claim a mutex per channel and release when its done
spin_lock_t channel_setup = SPIN_LOCK_INITIAL_VALUE;

typedef struct {
  channelState channels[MAX_CHANNELS];
  queue_t portsPendingAddress;
  struct list_node open_endpoints;
  struct list_node idle_channels;
} dwc_host_state_t;

// bitfield of each dev_addr to debug
static const int debug_device = BIT(1);

static bool show_debug(int daddr, int ep) {
  return false;
  if (BIT(daddr) & debug_device) {
    if (ep == 0) return true;
  }
  return false;
}

static const char *speeds[] = {
  [TUSB_SPEED_FULL] = "FS",
  [TUSB_SPEED_LOW] = "LS",
  [TUSB_SPEED_HIGH] = "HS"
};

static int dwc_root_enable(int argc, const console_cmd_args *argv);
static int dwc_root_disable(int argc, const console_cmd_args *argv);
static int dwc_root_reset(int argc, const console_cmd_args *argv);
static int dwc_show_masked_irq(int argc, const console_cmd_args *argv);
static void dump_channel(int i, const char *reason);
static struct dwc2_host_channel *get_channel(int i);
static void dwc_dump_all_state(void);
#ifdef UNUSED_CODE
static int dwc_cmd_show_state(int argc, const console_cmd_args *argv);
static int dwc_addr0_get_desc(int argc, const console_cmd_args *argv);
static int dwc_addr0_get_conf(int argc, const console_cmd_args *argv);
#endif
static void __attribute((noinline)) control_in(dwc_host_state_t *state, int channel, int addr, int endpoint, setupData *setup, uint32_t *buffer);
static void dwc_send_setup(dwc_host_state_t *state, int channel, int addr, int endpoint, setupData *setup, open_endpoint_t *opep);
static void dwc_host_in(dwc_host_state_t *state, int channel, int addr, int endpoint, uint32_t *buffer, uint32_t size, open_endpoint_t *opep);
static void dwc_host_out(dwc_host_state_t *state, int channel, int addr, int endpoint, uint32_t *buffer, uint32_t size, open_endpoint_t *opep);
static idle_channel_t *dwc_find_idle_channel(void);

static int dwc_get_speed(int argc, const console_cmd_args *argv) {
  tusb_speed_t speed = hcd_port_speed_get(0);
  logf("%s\n", speeds[speed]);
  return 0;
}

extern uint8_t _stext, _etext;

thread_t *shell_thread;

static int dwc_testit(int argc, const console_cmd_args *argv) {
  shell_thread = get_current_thread();
  bdev_t *dev = bio_open("usb2_0");
  STACKBUF_DMA_ALIGN(buffer, 512);
  assert(dev);

  uint32_t start = *REG32(ST_CLO);
  int blocks = 32;
  if (argc >= 2) blocks = argv[1].u;

  for (int i=0; i<blocks; i++) {
    //hash_blob(&sha256_implementation, &_stext, &_etext - &_stext, next_hash);
    //print_hash(next_hash, sha256_implementation.hash_size);
    bio_read_block(dev, buffer, i, 1);
  }
  uint32_t stop = *REG32(ST_CLO);
  printf("read %d blocks in %d uSec, %d uSec per block\n", blocks, stop - start, (stop-start)/blocks);

  bio_close(dev);
  return 0;
}

static void dump_fifo(uint32_t base, uint32_t size) {
  uintptr_t base_addr = USB_BASE + 0x20000 + (base * 4);
  uint32_t bytes = size*4;
  hexdump_ram((void*)base_addr, base_addr, bytes);
}

static uint32_t virtual_to_dma(void *buffer) {
  assert(((uintptr_t)buffer & 0x3) == 0);
#ifdef ARCH_VPU
  // TODO, retest this
  return (uint32_t)buffer;
#elif defined(ARCH_ARM64)
  addr_t phys_addr = vaddr_to_paddr(buffer);
  uint32_t truncated = (phys_addr & 0x3fffffff) | 0xc0000000;
  //logf("%p -> 0x%x, cacheline %d\n", buffer, truncated, CACHE_LINE);
  return truncated;
#endif
}

void dump_nptxfifo(void) {
  uint32_t t = *REG32(USB_GNPTXFSIZ);
  uint32_t base = t & 0xffff;
  uint32_t length = t >> 16;
  printf(GREEN"non periodic tx fifo 0x%x -> 0x%x (0x%x)\n"DEFAULT, base, base + length - 1, length);
  dump_fifo(base, length);
}

void dump_rxfifo(void) {
  uint32_t t = *REG32(USB_GRXFSIZ);
  uint32_t base = 0;
  uint32_t length = t & 0xffff;
  printf(GREEN"rx fifo 0x%x -> 0x%x\n"DEFAULT, base, base+length-1);
  dump_fifo(base, length);
}

STATIC_COMMAND_START
STATIC_COMMAND("dwc_root_enable", "enable the root port", &dwc_root_enable)
STATIC_COMMAND("dwc_root_disable", "disable the root port", &dwc_root_disable)
STATIC_COMMAND("dwc_root_reset", "reset the root port", &dwc_root_reset)
#ifdef UNUSED_CODE
STATIC_COMMAND("dwc_masked_irq", "show irq's that are firing but masked", &dwc_show_masked_irq)
STATIC_COMMAND("dwc_show_state", "show all register states", &dwc_cmd_show_state)
STATIC_COMMAND("addr0_get", "", &dwc_addr0_get_desc)
STATIC_COMMAND("addr0_get_conf", "", &dwc_addr0_get_conf)
#endif
STATIC_COMMAND("get_speed", "get speed", &dwc_get_speed)
STATIC_COMMAND("testit", "debug", &dwc_testit)
STATIC_COMMAND_END(dwc);

static dwc_host_state_t dwc_state;

#ifdef MEASURE_SOF
uint32_t last_sof = 10;
int measure_sof = 0;
int sof_total = 0;
#endif

deviceDescriptor devDesc __attribute__((aligned(4)));

#ifdef UNUSED_CODE
static int dwc_addr0_get_desc(int argc, const console_cmd_args *argv) {
  uint32_t read_length = 8;
  if (argc >= 2) {
    read_length = argv[1].u;
  }
  getDescriptorRequest setup;
  setup.bmRequestType = 0x80;
  setup.bRequest = 6;
  setup.bDescriptorIndex = 0;
  setup.bDescriptorType = 1;
  setup.wLanguageId = 0;
  setup.wLength = read_length;

  deviceDescriptor *devDesc2 = memalign(4, sizeof(deviceDescriptor));

  printf("expecting %d bytes\n", read_length);

  control_in(&dwc_state, 0, 0, 0, (setupData*)&setup, (uint32_t*)devDesc2);
  printf("  bLength: %d\n", devDesc2->bLength);
  printf("  bDescriptorType: %d\n", devDesc2->bDescriptorType);
  printf("  bcdUSB: 0x%x\n", devDesc2->bcdUSB);
  printf("  class: %d %d\n", devDesc2->bDeviceClass, devDesc2->bDeviceSubClass);
  printf("  protocol: %d\n", devDesc2->bDeviceProtocol);
  printf("  max-packet-size: %d\n", devDesc2->bMaxPacketSize0);
  if (read_length > 8) {
    printf("  VID:PID: %04x:%04x\n", devDesc2->idVendor, devDesc2->idProduct);
    printf("  bcdDevice: 0x%x\n", devDesc2->bcdDevice);
    printf("  iManufacturer/iProduct/iSerialNumber: %d/%d/%d\n", devDesc2->iManufacturer, devDesc2->iProduct, devDesc2->iSerialNumber);
    printf("  bNumberConfigurations: %d\n", devDesc2->bNumberConfigurations);
  }

  udelay(100 * 1000);
  logf("100ms later\n");
  dump_channel(0, __FUNCTION__);
  return 0;
}

static int dwc_addr0_get_conf(int argc, const console_cmd_args *argv) {
  getDescriptorRequest setup;
  configurationDescriptor config;
  void *buffer = memalign(4, sizeof(config));

  setup.bmRequestType = 0x80;
  setup.bRequest = 6;
  setup.bDescriptorIndex = 0;
  setup.bDescriptorType = 2;
  setup.wLanguageId = 0;
  setup.wLength = sizeof(config);

  control_in(&dwc_state, 0, 0, 0, (setupData*)&setup, (uint32_t*)buffer);
  memcpy(&config, buffer, sizeof(config));
  free(buffer);

  printf("  bLength: %d\n", config.bLength);
  printf("  bDescriptorType: %d\n", config.bDescriptorType);
  printf("  wTotalLength: %d\n", config.wTotalLength);
  printf("  bNumberInterfaces: %d\n", config.bNumberInterfaces);
  printf("  bConfigurationValue: %d\n", config.bConfigurationValue);
  printf("  iConfiguration: %d\n", config.iConfiguration);
  printf("  bmAttributes: %d\n", config.bmAttributes);
  printf("  bMaxPower: %d\n", config.bMaxPower);

  buffer = memalign(4, config.wTotalLength);
  setup.wLength = config.wTotalLength;

  control_in(&dwc_state, 0, 0, 0, (setupData*)&setup, (uint32_t*)buffer);

  interfaceDescriptor iface;
  memcpy(&iface, buffer+9, sizeof(iface));

  printf("iface 0:\n");
  printf("  bLength: %d\n", iface.bLength);
  printf("  bDescriptorType: %d\n", iface.bDescriptorType);
  printf("  bNumEndpoints: %d\n", iface.bNumEndpoints);

  endpointDescriptor ep;
  memcpy(&ep, buffer+9+9, sizeof(ep));
  printf("EP:\n");
  printf("  bLength: %d\n", ep.bLength);
  printf("  bDescriptorType: %d\n", ep.bDescriptorType);
  printf("  bEndpointAddress: %d\n", ep.bEndpointAddress);
  printf("  bmAttributes: %d\n", ep.bmAttributes);
  printf("  wMaxPacketSize: %d\n", ep.wMaxPacketSize);
  printf("  bInterval: %d\n", ep.bInterval);

  free(buffer);
  return 0;
}

static int dwc_cmd_show_state(int argc, const console_cmd_args *argv) {
  dwc_dump_all_state();
  return 0;
}
#endif

static void hprt_wc_bit(int bit) {
  uint32_t hprt = *REG32(USB_HPRT);
  hprt &= ~(BIT(7) | BIT(6) | BIT(5) | BIT(3) | BIT(2) | BIT(1));
  hprt |= BIT(bit);
  *REG32(USB_HPRT) = hprt;
}

static int dwc_show_masked_irq(int argc, const console_cmd_args *argv) {
  uint32_t t;
  uint32_t interrupt_status = *REG32(USB_GINTSTS);
  uint32_t mask = *REG32(USB_GINTMSK);
  printf("USB_GINTSTS: 0x%x\nUSB_GINTMSK: 0x%x\nmasked IRQ's: 0x%x\n", interrupt_status, mask, interrupt_status & ~mask);
  printf("active irq: 0x%x\n", interrupt_status & mask);
  if (mask & BIT(25)) puts("host channel allowed");
  if (interrupt_status & BIT(25)) puts("host channel active");

  uint32_t channel_int = *REG32(USB_HAINT);
  uint32_t channel_msk = *REG32(USB_HAINTMSK);
  printf("USB_HAINT: 0x%x\nUSB_HAINTMSK: 0x%x\nmasked IRQ's: 0x%x\n", channel_int, channel_msk, channel_int & ~channel_msk);

  dump_channel(0, __FUNCTION__);

  dumpreg(USB_GAHBCFG);
  dumpreg(USB_HPRT);
  return 0;
}

static int dwc_root_enable(int argc, const console_cmd_args *argv) {
  *REG32(USB_HPRT) = BIT(12);
  return 0;
}

static int dwc_root_disable(int argc, const console_cmd_args *argv) {
  *REG32(USB_HPRT) = BIT(2); // | BIT(12);
  return 0;
}

static int dwc_root_reset(int argc, const console_cmd_args *argv) {
  *REG32(USB_HPRT) = BIT(8) | BIT(12);
  udelay(50 * 1000);
  *REG32(USB_HPRT) = BIT(12);
  return 0;
}

void hcd_port_reset(uint8_t rhport) {
  //logf("reset on\n");
  *REG32(USB_HPRT) = BIT(8) | BIT(12);
}

void hcd_port_reset_end(uint8_t rhport) {
  //logf("reset off\n");
  *REG32(USB_HPRT) = BIT(12);
}

bool hcd_port_connect_status(uint8_t rhport) {
  return *REG32(USB_HPRT) & BIT(0);
}

tusb_speed_t hcd_port_speed_get(uint8_t rhport) {
  //uint32_t t;
  //dumpreg(USB_HPRT);
  switch ((*REG32(USB_HPRT) >> 17) & 0x3) {
  case 0: return TUSB_SPEED_HIGH;
  case 1: return TUSB_SPEED_FULL;
  case 2: return TUSB_SPEED_LOW;
  default: return TUSB_SPEED_INVALID;
  }
}

bool hcd_edpt_open(uint8_t rhport, uint8_t dev_addr, tusb_desc_endpoint_t const * ep_desc) {
  //logf(GREEN"dev %d EP%02x opened ", dev_addr, ep_desc->bEndpointAddress);
  open_endpoint_t *opep = malloc(sizeof(open_endpoint_t));
  opep->dev_addr = dev_addr;
  opep->ep_addr = ep_desc->bEndpointAddress & 0xf;
  opep->ep_type = ep_desc->bmAttributes.xfer;
  opep->max_packet_size = ep_desc->wMaxPacketSize;

  timer_initialize(&opep->retry);
  if (ep_desc->bmAttributes.xfer == 3) {  // interrupt
    opep->next_pid = 0;
  } else if (ep_desc->bmAttributes.xfer == 2) { // bulk
    opep->next_pid = 0;
  } else {
    opep->next_pid = 0x55;
  }

  hcd_devtree_get_info(dev_addr, &opep->info);
  //printf("root port: %d ", opep->info.rhport);
  //printf("hub_addr: %d ", opep->info.hub_addr);
  //printf("hub_port: %d ", opep->info.hub_port);
  //printf("speed: %d(%s) ", opep->info.speed, speeds[opep->info.speed]);
  //printf("opep: 0x%p\n"DEFAULT, opep);
  //print_endpoint(ep_desc);
  list_add_tail(&dwc_state.open_endpoints, &opep->l);
  return true;
}

void hcd_device_close(uint8_t rhport, uint8_t dev_addr) {
  //logf(GREEN"%d closed\n"DEFAULT, dev_addr);
  open_endpoint_t *opep, *next_opep;
  list_for_every_entry_safe(&dwc_state.open_endpoints, opep, next_opep, open_endpoint_t, l) {
    if (opep->dev_addr == dev_addr) {
      // TODO leaks the object and the timer
      list_delete(&opep->l);
    }
  }
}

open_endpoint_t *get_open_ep(uint8_t dev_addr, uint8_t ep_addr) {
  open_endpoint_t *opep;
  list_for_every_entry(&dwc_state.open_endpoints, opep, open_endpoint_t, l) {
    if ((opep->dev_addr == dev_addr) && (opep->ep_addr == ep_addr)) {
      return opep;
    }
  }
  logf(RED"Dev %d EP %02x not open!\n"DEFAULT, dev_addr, ep_addr);
  return NULL;
}

bool hcd_setup_send(uint8_t rhport, uint8_t dev_addr, uint8_t const setup_packet[8]) {
  uint32_t state;
  spin_lock_irqsave(&channel_setup, state);
  idle_channel_t *ic = dwc_find_idle_channel();
  open_endpoint_t *opep = get_open_ep(dev_addr, 0);
  opep->ic = ic;
  int channel = ic->channel;

  //hexdump_ram(setup_packet, (uint32_t)setup_packet, 16);

  if (show_debug(dev_addr, 0)) {
    logf(RED"\tHOST%d SETUP %d.%02x 0x%p/%d opep:0x%p\n"DEFAULT, channel, dev_addr, 0, setup_packet, 8, opep);
  }
  timer_cancel(&opep->retry);
  dwc_send_setup(&dwc_state, channel, dev_addr, 0, (setupData *)setup_packet, opep);
  spin_unlock_irqrestore(&channel_setup, state);

  return true;
}

bool hcd_edpt_xfer(uint8_t rhport, uint8_t dev_addr, uint8_t ep_addr, uint8_t * buffer, uint16_t buflen) {
  uint8_t epnr = ep_addr & 0xf;
  uint32_t state;
  //udelay(20); // TODO, if all logging is removed, enumeration fails

  spin_lock_irqsave(&channel_setup, state);
  idle_channel_t *ic = dwc_find_idle_channel();
  open_endpoint_t *opep = get_open_ep(dev_addr, epnr);
  assert(opep);
  opep->ic = ic;
  int channel = ic->channel;

  if (!opep) {
    printf("dev_addr %d epnrf %d lr=0x%p\n", dev_addr, epnr, __builtin_return_address(0));
  }

  assert(opep);
  timer_cancel(&opep->retry);

  if (ep_addr & 0x80) { // IN
    if (show_debug(dev_addr, epnr)) {
      logf(RED"\tHOST%d  <-   %d.%02x %p/%d opep:%p type:%d pid:%d\n"DEFAULT, channel, dev_addr, ep_addr, buffer, buflen, opep, opep->ep_type, opep->next_pid);
    }
    dwc_host_in(&dwc_state, channel, dev_addr, epnr, (uint32_t*)buffer, buflen, opep);
  } else { // OUT
    if (show_debug(dev_addr, epnr)) {
      logf(RED"\tHOST%d  ->   %d.%02x 0x%p/%d opep:0x%p type:%d pid:%d\n"DEFAULT, channel, dev_addr, ep_addr, buffer, buflen, opep, opep->ep_type, opep->next_pid);
    }
    dwc_host_out(&dwc_state, channel, dev_addr, epnr, (uint32_t*)buffer, buflen, opep);
  }
  //puts("releasing");
  spin_unlock_irqrestore(&channel_setup, state);
  return true;
}

bool hcd_edpt_abort_xfer(uint8_t rhport, uint8_t dev_addr, uint8_t ep_addr) {
  assert(0);
  while (true) {}
}

static enum handler_return dwc_in_retry(timer_t *t, lk_time_t now, void *arg) {
  uint32_t state;
  spin_lock_irqsave(&channel_setup, state);
  idle_channel_t *ic = dwc_find_idle_channel();
  // TODO, a 0-length OUT can NAK, and this doesnt deal with that

  open_endpoint_t *opep = arg;
  opep->ic = ic;
  int channel = ic->channel;
  int dev_addr = opep->dev_addr;
  void *buffer = opep->buffer;
  int buflen = opep->buflen;
  uint8_t epnr = opep->ep_addr & 0xf;

  if (show_debug(dev_addr, epnr)) {
    //logf(RED"\tHOST%d  <-   %d.%02x 0x%p/%d type:%d pid:%d opep:0x%p RETRY\n"DEFAULT, channel, dev_addr, epnr | 0x80, buffer, buflen, opep->ep_type, opep->next_pid, opep);
  }
  dwc_host_in(&dwc_state, channel, dev_addr, epnr, (uint32_t*)buffer, buflen, opep);
  spin_unlock_irqrestore(&channel_setup, state);
  return INT_NO_RESCHEDULE;
}

static idle_channel_t *dwc_find_idle_channel(void) {
  idle_channel_t *ic = list_remove_head_type(&dwc_state.idle_channels, idle_channel_t, node);
  //printf("found %p\n", ic);
  assert(ic);
  return ic;
  for (int i=1; i<8; i++) {
    struct dwc2_host_channel *chan = get_channel(i);

    if (chan->hcchar & BIT(31)) continue;
    if (chan->hcchar & BIT(30)) continue;
    printf(GREEN"selected channel %d\n"DEFAULT, i);
    //return i;
  }
  assert(0);
}



static enum handler_return dwc_check_interrupt(dwc_host_state_t *state) {
  enum handler_return ret = INT_NO_RESCHEDULE;
  uint32_t interrupt_status = *REG32(USB_GINTSTS);
  uint32_t ack = 0;
  //uint32_t t;

  //printf("irq: raw 0x%x respecting mask 0x%x\n", interrupt_status, interrupt_status & *REG32(USB_GINTMSK));

  if (interrupt_status & BIT(1)) {
    ack |= BIT(1);
    logf("access to register from wrong mode\n");
  }

  if (interrupt_status & BIT(2)) {
    uint32_t otg_status = *REG32(USB_GOTGINT);
    logf("USB_GOTGINT: 0x%x\n", otg_status);
    uint32_t otg_ack = 0;
    if (otg_status & BIT(18)) {
      otg_ack |= BIT(18);
      logf("A-Device Timeout Change\n");
    }

    if (otg_status & BIT(19)) {
      otg_ack |= BIT(19);
      logf("debounce done\n");
    }
    *REG32(USB_GOTGINT) = otg_ack;
  }

  if (interrupt_status & BIT(3)) {
    ack |= BIT(3);
    //uint32_t framenr = *REG32(USB_HFNUM) & 0xffff;
#ifdef MEASURE_SOF
    if (framenr % 100 == 0) {
      if (measure_sof < 100) {
        sof_total += *REG32(ST_CLO) - last_sof;
        measure_sof++;
      } else if (measure_sof == 100) {
        printf("avg sof: %d\n", sof_total);
        measure_sof++;
      }
      //printf("SOF interval: %d\n", *REG32(ST_CLO) - last_sof);
    }
    last_sof = *REG32(ST_CLO);
#endif
  }

#if DMA == 0
  if (interrupt_status & BIT(4)) {
    ack |= BIT(4);
    logf("RX irq\n");
    //dumpreg(USB_GRXSTSP);
    uint32_t sts = *REG32(USB_GRXSTSP);
    printf("USB_GRXSTSP: 0x%x\n", sts);
    int chan = sts & 0xf;
    int status = (sts >> 17) & 0xf;
    open_endpoint_t *opep = state->channels[chan].opep;
    //if ((opep->dev_addr == debug_device) && (status != 7)) {
      printf("  channel: %d\n", chan);
      printf("  byte count: %d\n", (sts >> 4) & 0x7ff);
      printf("  PID: %d\n", (sts >> 15) & 0x3);
      printf("  packet status: %d\n", status);
    //}
    if (status == 2) {
      //printf("IN packet, popping\n");
      uint32_t *dest_buf = state->channels[chan].buffer;
      uint32_t buffer_size = state->channels[chan].buffer_size;
      uint32_t expected_bytes = MIN((sts >> 4) & 0x7ff, buffer_size);
      for (unsigned int bytes = 0; bytes < expected_bytes; bytes += 4) {
        uint32_t word = *REG32(USB_DFIFO0);
        //printf("popped 0x%x 0x%x %d/%d\n", word, (uint32_t)dest_buf, bytes,expected_bytes);
        if ((((uint32_t)dest_buf & 0x3) == 0) && ((expected_bytes - bytes) >= 4)) {
          *dest_buf = word;
        } else {
          int bytes_rem = MIN(expected_bytes - bytes, 4);
          uint8_t *buf = (uint8_t*)dest_buf;
          for (int i=0; i<bytes_rem; i++) {
            //logf("i %d, bytes_rem %d, word 0x%x\n", i, bytes_rem, word);
            buf[i] = word & 0xff;
            word = word >> 8;
          }
        }
        dest_buf++;
      }
      ret = INT_RESCHEDULE;
    } else if (status == 3) {
      //logf("IN complete\n");
      struct dwc2_host_channel *channel = get_channel(chan);
      if (opep->ep_type == 3) {
        if (((sts >> 15) & 0x3) == 0) {
          opep->next_pid = 2;
        } else if (((sts >> 15) & 0x3) == 2) {
          opep->next_pid = 0;
        }
      }
      uint32_t hcchar = channel->hcchar;
      //uint32_t type = (hcchar >> 18) & 0x3;
      bool in = hcchar & BIT(15);
      uint8_t epnr = (hcchar >> 11) & 0xf;
      uint8_t devaddr = (hcchar >> 22) & 0x7f;
      uint32_t bytes = (sts >> 4) & 0x7ff;
      //logf("hcd_event_xfer_complete(%d, 0x%x, %d, success, true)\n", devaddr, epnr | (in ? 0x80 : 0), bytes);
      hcd_event_xfer_complete(devaddr, epnr | (in ? 0x80 : 0), bytes, XFER_RESULT_SUCCESS, true);
      ret = INT_RESCHEDULE;
    } else if (status == 7) {
      // channel halted
    } else {
      puts("unhandled packet status on rx fifo");
      printf("  channel: %d\n", chan);
      printf("  byte count: %d\n", (sts >> 4) & 0x7ff);
      printf("  PID: %d\n", (sts >> 15) & 0x3);
      printf("  packet status: %d\n", status);
    }
  }
#endif

  if (interrupt_status & BIT(8)) {
    ack |= BIT(8);
    logf("ULPI Carkit Interrupt");
  }

  if (interrupt_status & BIT(24)) {
    uint32_t hprt = *REG32(USB_HPRT);
    hprt &= BIT(5) | BIT(3) | BIT(2) | BIT(1);
    //logf("masked HPRT: 0x%x\n", hprt);
    if (hprt & BIT(1)) {
      hprt_wc_bit(1);
      //tusb_speed_t speed = hcd_port_speed_get(0);
      //logf("Port Connect Detected at %s\n", speeds[speed]);
      hcd_event_device_attach(0, true);
      ret = INT_RESCHEDULE;
    }

    if (hprt & BIT(3)) {
      //logf("Port Enable/Disable Change\n");
      //dumpreg(USB_HPRT);


      if (*REG32(USB_HPRT) & BIT(2)) {
        tusb_speed_t speed = hcd_port_speed_get(0);
        //logf("port enabled at %s\n", speeds[speed]);
        if (speed == TUSB_SPEED_HIGH) {
          *REG32(USB_HFIR) = 7500;
        }
      } else {
        //logf("port disabled\n");
      }

      hprt_wc_bit(3);

      //dumpreg(USB_HPRT);
    }
    if (*REG32(USB_HPRT) & BIT(0)) {
      //logf("a device is connected\n");
    }
  }

  if (interrupt_status & BIT(25)) {
    //logf("host channel interrupt\n");
    //dumpreg(USB_HAINT);    // 414
    uint32_t channelmask = *REG32(USB_HAINT);
    for (int i=0; i<16; i++) {
      if (channelmask & BIT(i)) {
        open_endpoint_t *opep = state->channels[i].opep;
        struct dwc2_host_channel *chan = get_channel(i);

        uint32_t int_flags = chan->hcint;
        if (!opep) {
          //printf("HOST%d opep: %p, HCINT 0x%x\n", i, opep, int_flags);
          //dump_channel(i, "opep invalid");
          // for an unknown reason, the halt interrupt seems to sometimes fire twice on arm
          chan->hcint = int_flags;
          continue;
        }
        assert(opep);

        if (show_debug(opep->dev_addr, opep->ep_addr & 0xf)) {
          uint32_t now = *REG32(ST_CLO);
          printf(RED"channel %d wants attention after %d uSec\n"DEFAULT, i, now - state->channels[i].req_start);
          dump_channel(i, "wants att");
          printf("acking 0x%x\n", int_flags);
        }

        if (int_flags & BIT(4)) {
          //logf("NAK on chan %d\n", i);
        }
        chan->hcint = int_flags;

        uint32_t hcchar = chan->hcchar;
        uint32_t type = (hcchar >> 18) & 0x3;
        bool in = hcchar & BIT(15);
        uint8_t epnr = (hcchar >> 11) & 0xf;
        uint8_t devaddr = (hcchar >> 22) & 0x7f;
        uint32_t bytes = chan->hctsiz & 0x7ffff;

        if ((int_flags == (BIT(4) | BIT(1))) && (type == 3)) {
          // interrupt endpoint NAK + HALT

          // attempt a restart
          timer_set_oneshot(&opep->retry, 10000, dwc_in_retry, opep);
          list_add_tail(&dwc_state.idle_channels, &opep->ic->node);
          //chan->hcchar = BIT(31) | BIT(30);
        } else if (int_flags & BIT(0)) { // transfer completed
          int total_size = opep->buflen - bytes;
          if ((!in) && (total_size == 0)) {
            total_size = opep->buflen;
          }
          if (total_size < 0) total_size = 0;
          int packets_remaining = (chan->hctsiz >> 19) & 0x3ff;
          int packets_moved = opep->packets - packets_remaining;

          if (show_debug(opep->dev_addr, opep->ep_addr & 0xf)) {
            if (total_size) {
              hexdump_ram(opep->buffer, 0 /*(uintptr_t)opep->buffer*/, total_size+15);
            }
            dump_channel(i, "xfer comp");
            logf("bytes %d, buflen %d, packets: %d, old next_pid: %d\n", bytes, opep->buflen, opep->packets, opep->next_pid);
            logf("hcd_event_xfer_complete(%d, 0x%x, %d, success, true)\n", devaddr, epnr | (in ? 0x80 : 0), total_size);
          }
          if (packets_moved & 1) {
            if (opep->ep_type == 0) {
              if (((chan->hctsiz >> 29) & 3) == 1) opep->next_pid = 2;
            } else {
              if (opep->next_pid == 0) opep->next_pid = 2;
              else if (opep->next_pid == 2) opep->next_pid = 0;
            }
          }
          timer_cancel(&opep->retry);
          state->channels[i].opep = NULL;
          list_add_tail(&dwc_state.idle_channels, &opep->ic->node);
          // TODO, is bytes not always correct?
          hcd_event_xfer_complete(devaddr, epnr | (in ? 0x80 : 0), total_size, XFER_RESULT_SUCCESS, true);
          ret = INT_RESCHEDULE;
          //}
        } else if (int_flags & BIT(10)) {
          logf("HOST%d data toggle error\n", i);
          assert(0);
        } else if (int_flags & BIT(4)) {
          //logf(RED"NAK chan:%d ep:%d opep:0x%x %d %d\n"DEFAULT, i, opep->ep_addr, (uint32_t)opep, opep->dev_addr, opep->ep_addr);
          // in PIO mode, NAK hangs, you must either HALT or somehow restart
          // in DMA mode, NAK isnt fatal, and it auto-retries
          //opep->buffer = state->channels[i].buffer;
          //opep->buflen = state->channels[i].buffer_size;
          //timer_set_oneshot(&opep->retry, 10, dwc_in_retry, opep);
          //chan->hcchar = BIT(31) | BIT(30);
          //chan->hctsiz = 0;
          //chan->hcchar = BIT(30);
        } else if (int_flags & BIT(9)) {
          //logf("frame overrun\n");
          // not sure on the cause, can just retry
          // current theory, is that a transfer was started too late in a (micro)frame
          // SOF had to interrupt it
          opep->buffer = state->channels[i].buffer;
          opep->buflen = state->channels[i].buffer_size;
          timer_set_oneshot(&opep->retry, 100, dwc_in_retry, opep);
          chan->hcchar = BIT(31) | BIT(30);
          chan->hctsiz = 0;
          list_add_tail(&dwc_state.idle_channels, &opep->ic->node);
        } else if (int_flags & BIT(7)) {
          logf("transaction error on channel %d\n", i);
          list_add_tail(&dwc_state.idle_channels, &opep->ic->node);
          chan->hcchar = BIT(31) | BIT(30);
          logf("hcd_event_xfer_complete(%d, 0x%x, %d, fail, true)\n", devaddr, epnr | (in ? 0x80 : 0), bytes);
          hcd_event_xfer_complete(devaddr, epnr | (in ? 0x80 : 0), bytes, XFER_RESULT_FAILED, true);
          ret = INT_RESCHEDULE;
        } /*else if ((type == 0) && (!in)) { // control
          printf("acked 0x%x\n", int_flags);
          assert(0);
          //dump_channel(i);
          //logf("hcd_event_xfer_complete(%d, 0x%x, %d, success, true)\n", devaddr, epnr | (in ? 0x80 : 0), bytes);
          if (opep->ep_type == 3) {
            //dump_channel(i);
            //opep->next_pid = ((chan->hctsiz >> 29) & 0x3);
          }
          state->channels[i].opep = NULL;
          hcd_event_xfer_complete(devaddr, epnr | (in ? 0x80 : 0), bytes, XFER_RESULT_SUCCESS, true);
        }*/
        if (int_flags & BIT(1)) {
          // halt complete
          //dump_channel(i, "halt finished");
        }

        ret = INT_RESCHEDULE;
      }
    }
  }

  if (interrupt_status & BIT(28)) {
    ack |= BIT(28);
    //logf("Connector ID Status Change\n");
  }

  if (interrupt_status & BIT(29)) {
    ack |= BIT(29);
    logf("Disconnect Detected Interrupt\n");
    hcd_event_device_remove(0, true);
    ret = INT_RESCHEDULE;
  }

  if (interrupt_status & BIT(30)) {
    ack |= BIT(30);
    //logf("Session Request/New Session Detected Interrupt\n");
  }

  *REG32(USB_GINTSTS) = ack;

  return ret;
}

static enum handler_return dwc_irq(dwc_host_state_t *state) {
  lk_bigtime_t start = current_time_hires();
  enum handler_return ret = dwc_check_interrupt(state);
  lk_bigtime_t end = current_time_hires();
  lk_bigtime_t spent = end - start;
  if (spent > 20) logf("irq time: %lld\n", spent);
  return ret;
}

void dwc_root_port_reset(void) {
  *REG32(USB_HPRT) = BIT(8) | BIT(12);
  udelay(50 * 1000);
  *REG32(USB_HPRT) = BIT(12);
}

static struct dwc2_host_channel *get_channel(int i) {
  return (struct dwc2_host_channel *)(USB_BASE + 0x500 + (i * 0x20));
}

static void dump_channel(int i, const char * reason) {
  struct dwc2_host_channel *chan = get_channel(i);
  uint32_t t;

  printf("dumping channel %d: %s\n", i, reason);
  t = chan->hcchar;
  printf("  HCCHAR%d: 0x%x\n", i, t);
  printf("    mps:%d ep:%d dir%d type:%d mc:%d addr:%d\n", t & 0x7ff, (t >> 11) & 0xf, (t >> 15) & 1, (t >> 18) & 3, (t >> 20) & 3, (t >> 22) & 0x7f);
  printf("  HCSPLT%d: 0x%x\n", i, chan->hcsplt);
  t = chan->hcint;
  printf("  HCINT%d: 0x%x\n", i, t);
  if (t & BIT(0)) printf("    xfer complete\n");
  if (t & BIT(1)) printf("    halted\n");
  if (t & BIT(4)) printf("    NAK\n");
  if (t & BIT(3)) printf("    STALL\n");
  if (t & BIT(5)) printf("    ack\n");
  if (t & BIT(7)) printf("    transaction error\n");
  printf("  HCINTMSK%d: 0x%x\n", i, chan->hcintmsk);
  t = chan->hctsiz;
  printf("  HCTSIZ%d: 0x%x\n", i, t);
  printf("    size:%d packets:%d pid:%d ping:%d\n", t & 0x7ffff, (t >> 19) & 0x3ff, (t >> 29) & 0x3, (t >> 31) & 1);
  printf("  HCDMA%d: 0x%08x\n", i, chan->hcdma);
}

static void dwc_send_setup(dwc_host_state_t *state, int channel, int addr, int endpoint, setupData *setup, open_endpoint_t *opep) {
  struct dwc2_host_channel *chan = get_channel(channel);

  arch_clean_cache_range((addr_t)setup, 8);

  //thread_sleep(500);
  if (show_debug(addr, 0)) {
    logf(RED"dwc_send_setup(0x%p, %d, %d, %d, 0x%p)\n"DEFAULT, state, channel, addr, endpoint, setup);
    dump_channel(channel, "setup pre");
  }
  assert((chan->hcchar & HCCHARn_ENABLE) == 0);

  state->channels[channel].req_start = *REG32(ST_CLO);
  state->channels[channel].opep = opep;
  opep->buflen = 8;
  opep->packets = 1;
  opep->next_pid = 3;
  opep->buffer = setup;

  chan->hctsiz = HCTSIZn_BYTES(8) | HCTSIZn_PACKET_COUNT(1) | HCTSIZn_PID(3);
  chan->hcsplt = 0;
  chan->hcdma = virtual_to_dma(setup);
  chan->hcint = 0xffff;
  chan->hcintmsk = 0xffff;
  chan->hcchar = HCCHARn_MAX_PACKET_SIZE(opep->max_packet_size) | HCCHARn_ENDPOINT(endpoint) | HCCHARn_ADDR(addr) | (1<<20) | HCCHARn_OUT;
  if (show_debug(addr, 0)) {
    dump_channel(channel, "setup init");
  }
  chan->hcchar |= HCCHARn_ENABLE;
  //dumpreg(USB_GNPTXSTS); // 2c

#if !DMA
  uint32_t *src = (uint32_t*)setup;
  uint32_t *dest = (uint32_t*)USB_DFIFO(channel);
  dest[0] = src[0];
  dest[0] = src[1];
#endif
  //dumpreg(USB_GNPTXSTS); // 2c
  //thread_sleep(10);
  //dwc_show_masked_irq(0, NULL);
}

static void dwc_host_in(dwc_host_state_t *state, int channel, int addr, int endpoint, uint32_t *buffer, uint32_t size, open_endpoint_t *opep) {
  struct dwc2_host_channel *chan = get_channel(channel);
  int max_packet_size = opep->max_packet_size;
  int type = opep->ep_type;
  int pid = 2;

#ifdef ARCH_ARM64
  if (size > 0) {
    arch_clean_invalidate_cache_range((addr_t)buffer, size);
  }
#endif

  if (endpoint == 1) {
    pid = 0;
    //logf("type %d, mps %d, addr %d, pid %d, buffer 0x%x\n", type, max_packet_size, addr, pid, (uint32_t)buffer);
  }
  if (opep->ep_type == 0) { // control-in data state
    pid = 2;
  } else if (type == 2) { // bulk
    pid = opep->next_pid;
  } else if (type == 3) { // interrupt
    pid = opep->next_pid;
  }

  int packets = ( ((int)size-1) / max_packet_size) + 1;

  state->channels[channel].req_start = *REG32(ST_CLO);
  state->channels[channel].buffer = buffer;
  state->channels[channel].buffer_size = size;
  state->channels[channel].opep = opep;
  opep->buflen = size;
  opep->packets = packets;
  opep->buffer = buffer;


  if (show_debug(addr, endpoint & 0xf)) {
    printf("size %d, mps %d, packets %d, pid %d/%d, type %d\n", size, opep->max_packet_size, packets, pid, opep->next_pid, opep->ep_type);
  }

  // TODO handle the device responding with fewer bytes then expected
  chan->hctsiz = HCTSIZn_BYTES(size) | HCTSIZn_PACKET_COUNT(packets) | HCTSIZn_PID(pid);
  if (type == 2) {
    //chan->hctsiz |= BIT(31);
  }
  chan->hcsplt = 0;
  chan->hcint = 0xffff;
  chan->hcdma = virtual_to_dma(buffer);
  chan->hcintmsk = 0xffff & ~BIT(4);
  chan->hcchar = HCCHARn_MAX_PACKET_SIZE(max_packet_size) | HCCHARn_ENDPOINT(endpoint) | HCCHARn_ADDR(addr) | HCCHARn_IN | (1<<20) | (type << 18);

  //THREAD_LOCK(state2);
  if (show_debug(addr, endpoint & 0xf)) {
    dump_channel(channel, "IN init");
  }
  //puts("enabling...");
  //dump_channel(channel, "IN init2");
  chan->hcchar |= HCCHARn_ENABLE;
  //puts("enabled");
  //THREAD_UNLOCK(state2);
}

static void dwc_host_out(dwc_host_state_t *state, int channel, int addr, int endpoint, uint32_t *buffer, uint32_t size, open_endpoint_t *opep) {
  struct dwc2_host_channel *chan = get_channel(channel);

#ifdef ARCH_ARM64
  if (size > 0) {
    arch_clean_invalidate_cache_range((addr_t)buffer, size);
  }
#endif

  if (show_debug(addr, endpoint & 0xf)) {
    logf("size=%d\n", size);
    dump_channel(channel, "out pre");
  }
  assert((chan->hcchar & HCCHARn_ENABLE) == 0);

  int mps = opep->max_packet_size;
  int type = 0; // TODO
  int packets = (size / mps)+1;
  int last_packet = size - ((packets-1)*mps);
  //printf("OUT size %d, mps %d, packets %d, last_packet %d\n", size, mps, packets, last_packet);
  opep->buflen = size;
  opep->buffer = buffer;
  opep->packets = packets;
  state->channels[channel].opep = opep;

  if (size != 0) assert(last_packet > 0);

  state->channels[channel].req_start = *REG32(ST_CLO);
  chan->hctsiz = HCTSIZn_BYTES(size) | HCTSIZn_PACKET_COUNT(packets) | HCTSIZn_PID(opep->next_pid);
  chan->hcsplt = 0;
  chan->hcint = 0xffff;
  chan->hcintmsk = 0xffff;
  chan->hcdma = virtual_to_dma(buffer);
  chan->hcchar = HCCHARn_MAX_PACKET_SIZE(mps) | HCCHARn_ENDPOINT(endpoint) | HCCHARn_ADDR(addr) | (1<<20) | HCCHARn_OUT | (type << 18);
  if (show_debug(addr, endpoint & 0xf)) {
    dump_channel(channel, "OUT init");
  }
  chan->hcchar |= HCCHARn_ENABLE;

#if !DMA
  uint32_t *dest = (uint32_t*)USB_DFIFO(channel);

  for (unsigned int sent=0; sent < size; sent += 4) {
    uint32_t t = buffer[sent/4];
    printf("0x%x\n", t);
    dest[0] = t;
  }
#endif
}

static void control_in(dwc_host_state_t *state, int channel, int addr, int endpoint, setupData *setup, uint32_t *buffer) {
  dwc_send_setup(state, channel, addr, endpoint, setup, NULL);

  dwc_host_in(state, channel, addr, endpoint, buffer, setup->wLength, NULL);

  dwc_host_out(state, channel, addr, endpoint, NULL, 0, NULL);
}

static void dwc_dump_all_state(void) {
  uint32_t t;
  bool host_mode = false;
  bool device_mode = false;

  dumpreg(USB_GOTGCTL); // 00
  dumpreg(USB_GOTGINT); // 04
  dumpreg(USB_GAHBCFG); // 08
  dumpreg(USB_GUSBCFG); // 0c

  dumpreg(USB_GRSTCTL); // 10
  dumpreg(USB_GINTSTS); // 14
  if (t & BIT(0)) {
    host_mode = true;
  } else {
    device_mode = true;
  }
  dumpreg(USB_GINTMSK); // 18
  dumpreg(USB_GRXSTSR); // 1c

  // 20 is a FIFO
  dumpreg(USB_GRXFSIZ); // 24
  dumpreg(USB_GNPTXFSIZ); // 28
  dumpreg(USB_GNPTXSTS); // 2c

  dumpreg(USB_GPVNDCTL); // 34
  dumpreg(USB_GUID);     // 3c

  dumpreg(USB_GHWCFG1);  // 44
  if (t == 0) puts("  all endpoints bi-directional");
  dumpreg(USB_GHWCFG2);   // 48
  printf("  OtgMode: %d\n", t & 7);
  printf("  OtgArch: %d\n", (t >> 3) & 3);
  printf("  NumHstChnl: %d\n", (t >> 14) & 0xf);
  if (t & (1<<5)) puts("  single-point\n");
  dumpreg(USB_GHWCFG3);   // 4c
  dumpreg(USB_GHWCFG4);   // 50

  dumpreg(USB_HPTXFSIZ);  // 100

  if (host_mode) {
    dumpreg(USB_HCFG);    // 400
    // for 60mhz clock and 125uSec interval, use 7500
    dumpreg(USB_HFIR);    // 404
    dumpreg(USB_HFNUM);   // 408

    dumpreg(USB_HPTXSTS); // 410

    dumpreg(USB_HPRT);    // 440
    if (t & BIT(0)) puts("  device attached");

    for (int i=0; i < MAX_CHANNELS; i++) {
      dump_channel(i, "dump state");
    }
  }

  if (device_mode) {
  }

  printf("rx fifo depth: 0x%x\n", *REG32(USB_GRXFSIZ));
  t = *REG32(USB_GNPTXFSIZ);
  printf("non-periodic tx fifo depth: 0x%x\n", t >> 16);
  printf("non-periodic tx fifo start: 0x%x\n", t & 0xffff);
  t = *REG32(USB_GNPTXSTS);
  printf("non-periodic tx fifo space available: 0x%x\n", t & 0xffff);
  printf("non-periodic tx request queue space available: 0x%x\n", (t >> 16) & 0xff);
  printf("top entry: 0x%x\n", (t >> 24) & 0x7f);
  t = *REG32(USB_HPTXFSIZ);
  printf("host periodic tx start: 0x%x\n", t & 0xffff);
  printf("host periodic tx fifo depth: 0x%x\n", (t >> 16) & 0xffff);
}

bool hcd_init(uint8_t rhport) {
  //uint32_t t;

  //dumpreg(USB_PCGCCTL);

  //*REG32(USB_GRSTCTL) = BIT(5) | BIT(4) | BIT(3) | BIT(2) | BIT(1) | BIT(0);
  *REG32(USB_GRSTCTL) = BIT(0);

  //dumpreg(USB_GRSTCTL);

  while (*REG32(USB_GRSTCTL) & BIT(0)) {}
  //logf("reset completed\n");

  // even if reading claims the bit is already set, it doesnt work until you set it again
  // TODO, try turning dma on now
  *REG32(USB_GAHBCFG) = BIT(0)
#if DMA
  | BIT(5)
  | (3 << 1) // burst type INCR4
#endif
  ;

  //dumpreg(USB_GRSTCTL);
  //dumpreg(USB_PCGCCTL);

  thread_sleep(50);

  list_initialize(&dwc_state.idle_channels);

  uint32_t ghwcfg2 = *REG32(USB_GHWCFG2);
  int channel_count = ((ghwcfg2 >> 14) & 0xf) + 1;
  printf("controller has %d host channels\n", channel_count);
  for (int i=0; i<channel_count; i++) {
    idle_channel_t *c = malloc(sizeof(idle_channel_t));
    c->channel = i;
    list_add_tail(&dwc_state.idle_channels, &c->node);
  }

  // re-configure all FIFO's
  *REG32(USB_GRXFSIZ) =                   0x0e00;
  *REG32(USB_GNPTXFSIZ) = (0x100 << 16) | 0x1000;
  *REG32(USB_HPTXFSIZ) =  (0x200 << 16) | 0x2000;

  *REG32(USB_GRSTCTL) = (0x10 << 6) | BIT(5);
  while (*REG32(USB_GRSTCTL) & BIT(5)) {}

  *REG32(USB_GRSTCTL) = BIT(4);
  while (*REG32(USB_GRSTCTL) & BIT(4)) {}
  // fifo reset done

  register_int_handler(DWC_IRQ, (int_handler)dwc_irq, &dwc_state);

  *REG32(USB_GINTMSK) = BIT(1) | BIT(2) | /* BIT(3) |*/
#if DMA == 0
    BIT(4) |
#endif
    USB_GINTMSK_PortInt | BIT(25) | BIT(28) | BIT(29) | BIT(30);
  *REG32(USB_GINTSTS) = BIT(1) | BIT(11) | BIT(12) | BIT(15); // clear some interrupts
  *REG32(USB_HAINTMSK) = 0xffff;

  *REG32(USB_HCFG) = 0;

  // turn VBUS on
  *REG32(USB_HPRT) = BIT(12);

  //thread_sleep(100);

  return true;
}

void hcd_int_enable (uint8_t rhport) {
  //dwc_show_masked_irq(0, NULL);
  unmask_interrupt(DWC_IRQ);
  //logf("int unmasked\n");
  //dwc_show_masked_irq(0, NULL);
}

void hcd_int_disable(uint8_t rhport) {
  mask_interrupt(DWC_IRQ);
}

bool hcd_configure(uint8_t rhport, uint32_t cfg_id, const void* cfg_param) {
  return false;
}

#define PRINTREG(x) t = *REG32(x); printf( #x ": 0x%x\n", t)
static void dwc2_init_hook(uint level) {
  puts("dwc2 init hook");
  uint32_t t;

  //dwc_dump_all_state();


  logf("queue init\n");
  queue_init(&dwc_state.portsPendingAddress);
  list_initialize(&dwc_state.open_endpoints);



  PRINTREG(USB_HAINT);    // 414
  PRINTREG(USB_HAINTMSK); // 418
  //dumpreg(USB_GAHBCFG);
  //*REG32(USB_GAHBCFG) = BIT(0) | BIT(5);
  //dumpreg(USB_GAHBCFG);


  printf("masked irq's: 0x%x\n", *REG32(USB_GINTSTS) & ~(*REG32(USB_GINTMSK)));

  if (0) {
    dwc_root_port_reset();
    logf("reset done\n");

    getDescriptorRequest setup;
    setup.bmRequestType = 0x80;
    setup.bRequest = 6;
    setup.bDescriptorIndex = 0;
    setup.bDescriptorType = 1;
    setup.wLanguageId = 0;
    setup.wLength = 8;

    control_in(&dwc_state, 0, 0, 0, (setupData*)&setup, (uint32_t*)&devDesc);
    printf("  bLength: %d\n", devDesc.bLength);
    printf("  bDescriptorType: %d\n", devDesc.bDescriptorType);
    printf("  bcdUSB: 0x%x\n", devDesc.bcdUSB);
    printf("  class: %d %d\n", devDesc.bDeviceClass, devDesc.bDeviceSubClass);
    printf("  protocol: %d\n", devDesc.bDeviceProtocol);
    printf("  max-packet-size: %d\n", devDesc.bMaxPacketSize0);

    udelay(100 * 1000);
    logf("100ms later\n");
    dump_channel(0, "unused");
  }
}

LK_INIT_HOOK(dwc2, &dwc2_init_hook, LK_INIT_LEVEL_PLATFORM + 6);
