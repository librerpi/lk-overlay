#include <assert.h>
#include <dev/gpio.h>
#include <kernel/event.h>
#include <kernel/mutex.h>
#include <lib/heap.h>
#include <lk/console_cmd.h>
#include <lk/err.h>
#include <lk/init.h>
#include <lk/reg.h>
#include <platform/bcm28xx/clock.h>
#include <platform/bcm28xx/dwc2.h>
#include <platform/bcm28xx/gpio.h>
#include <platform/bcm28xx/otp.h>
#include <platform/bcm28xx/print_timestamp.h>
#include <platform/bcm28xx/queue.h>
#include <platform/bcm28xx/udelay.h>
#include <platform/bcm28xx/usb.h>
#include <platform/interrupts.h>
#include <platform/time.h>
#include <stdio.h>
#include <stdlib.h>
#include <tusb.h>
#include <host/hcd.h>

#define logf(fmt, ...) { print_timestamp(); printf("[DWC2:%s:%d]: "fmt, __FUNCTION__, __LINE__, ##__VA_ARGS__); }
#define BIT(b) (1 << b)
#define dumpreg(reg) { t = *REG32(reg); printf(#reg":\t 0x%x\n", t); }

#define CSI "\x1b["
#define RED     CSI"31m"
#define DEFAULT CSI"39m"

struct dwc2_host_channel {
  volatile uint32_t hcchar;
  volatile uint32_t hcsplt;
  volatile uint32_t hcint;
  volatile uint32_t hcintmsk;

  volatile uint32_t hctsiz;
  volatile uint32_t hcdma;
};

typedef struct {
  deviceDescriptor devDescriptor;
  uint8_t addr;
  uint32_t max_packet_size;
} deviceState;

typedef struct {
  event_t irq;
  uint32_t *buffer;
  uint32_t buffer_size;
  uint32_t req_start;
} channelState;

typedef struct {
  struct list_node l;
  bool root_port;
} pendingPort_t;

#define MAX_DEVICES 10
#define MAX_CHANNELS 16

typedef struct {
  deviceState *devices[MAX_DEVICES];
  channelState channels[MAX_CHANNELS];
  uint32_t nextDevice;
  uint32_t nextAddress;
  thread_t *addressAssigner;
  queue_t portsPendingAddress;
} dwc_host_state_t;

const int debug_channel = 5;

static int dwc_root_enable(int argc, const console_cmd_args *argv);
static int dwc_root_disable(int argc, const console_cmd_args *argv);
static int dwc_root_reset(int argc, const console_cmd_args *argv);
static int dwc_show_masked_irq(int argc, const console_cmd_args *argv);
static void dump_channel(int i);
static struct dwc2_host_channel *get_channel(int i);
static void dwc_dump_all_state(void);
static int dwc_cmd_show_state(int argc, const console_cmd_args *argv);
static int rpi_lan_run(int argc, const console_cmd_args *argv);
static int dwc_addr0_get_desc(int argc, const console_cmd_args *argv);
static int dwc_addr0_get_conf(int argc, const console_cmd_args *argv);
static void __attribute((noinline)) control_in(dwc_host_state_t *state, int channel, int addr, int endpoint, setupData *setup, uint32_t *buffer);
static void dwc_send_setup(dwc_host_state_t *state, int channel, int addr, int endpoint, setupData *setup, bool blocking);
static void dwc_host_in(dwc_host_state_t *state, int channel, int addr, int endpoint, uint32_t *buffer, uint32_t size, bool blocking);
static void dwc_host_out(dwc_host_state_t *state, int channel, int addr, int endpoint, uint32_t *buffer, uint32_t size, bool blocking);
static int dwc_find_idle_channel(void);

STATIC_COMMAND_START
STATIC_COMMAND("dwc_root_enable", "enable the root port", &dwc_root_enable)
STATIC_COMMAND("dwc_root_disable", "disable the root port", &dwc_root_disable)
STATIC_COMMAND("dwc_root_reset", "reset the root port", &dwc_root_reset)
STATIC_COMMAND("dwc_masked_irq", "show irq's that are firing but masked", &dwc_show_masked_irq)
STATIC_COMMAND("dwc_show_state", "show all register states", &dwc_cmd_show_state)
STATIC_COMMAND("lan_run", "set the LAN_RUN pin", &rpi_lan_run)
STATIC_COMMAND("addr0_get", "", &dwc_addr0_get_desc)
STATIC_COMMAND("addr0_get_conf", "", &dwc_addr0_get_conf)
STATIC_COMMAND_END(dwc);

static dwc_host_state_t dwc_state;

uint32_t last_sof = 0;
deviceDescriptor devDesc __attribute__((aligned(4)));

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
  dump_channel(0);
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

static void lan_run(int level);

static int rpi_lan_run(int argc, const console_cmd_args *argv) {
  if (argc != 2) {
    printf("usage: lan_run <0|1>\n");
    return 0;
  }
  lan_run(argv[1].u);
  return 0;
}

static void lan_run(int level) {
  uint32_t revision = otp_read(30);
  uint32_t type = (revision >> 4) & 0xff;
  int lan_run = 0;

  switch (type) {
  case 4: // 2B
    lan_run = 31;
    break;
  case 8: // 3B
    lan_run = 29;
    break;
  case 0xd: // 3B+
    lan_run = 30;
    break;
  }

  if (lan_run > 0) {
    gpio_config(lan_run, kBCM2708PinmuxOut);
    gpio_set(lan_run, level);
  }
}

static void hprt_wc_bit(int bit) {
  uint32_t hprt = *REG32(USB_HPRT);
  hprt &= ~(BIT(7) | BIT(6) | BIT(5) | BIT(3) | BIT(2) | BIT(1));
  hprt |= BIT(bit);
  *REG32(USB_HPRT) = hprt;
}

static int dwc_show_masked_irq(int argc, const console_cmd_args *argv) {
  uint32_t interrupt_status = *REG32(USB_GINTSTS);
  uint32_t mask = *REG32(USB_GINTMSK);
  printf("USB_GINTSTS: 0x%x\nUSB_GINTMSK: 0x%x\nmasked IRQ's: 0x%x\n", interrupt_status, mask, interrupt_status & ~mask);
  return 0;
}

static int dwc_cmd_show_state(int argc, const console_cmd_args *argv) {
  dwc_dump_all_state();
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
  *REG32(USB_HPRT) = BIT(8) | BIT(12);
}

void hcd_port_reset_end(uint8_t rhport) {
  *REG32(USB_HPRT) = BIT(12);
}

bool hcd_port_connect_status(uint8_t rhport) {
  return *REG32(USB_HPRT) & BIT(0);
}

bool hcd_setup_send(uint8_t rhport, uint8_t dev_addr, uint8_t const setup_packet[8]) {
  //thread_sleep(500); // TODO, wait for channel to be free, or pick another channel
  int channel = dwc_find_idle_channel();

  logf(RED"\tHOST%d SETUP %d.%02x 0x%x/%d\n"DEFAULT, channel, dev_addr, 0, (uint32_t)setup_packet, 8);
  dwc_send_setup(&dwc_state, channel, dev_addr, 0, (setupData *)setup_packet, false);

  return true;
}

tusb_speed_t hcd_port_speed_get(uint8_t rhport) {
  switch ((*REG32(USB_HPRT) >> 17) & 0x3) {
  case 0: return TUSB_SPEED_HIGH;
  case 1: return TUSB_SPEED_FULL;
  case 2: return TUSB_SPEED_LOW;
  default: return TUSB_SPEED_INVALID;
  }
}

bool hcd_edpt_open(uint8_t rhport, uint8_t dev_addr, tusb_desc_endpoint_t const * ep_desc) {
  logf("dev %d EP%02x opened\n", dev_addr, ep_desc->bEndpointAddress);
  hcd_devtree_info_t info;
  hcd_devtree_get_info(dev_addr, &info);
  printf("  root port: %d\n", info.rhport);
  printf("  hub_addr: %d\n", info.hub_addr);
  printf("  hub_port: %d\n", info.hub_port);
  printf("  speed: %d\n", info.speed);
  // TODO
  return true;
}

void hcd_device_close(uint8_t rhport, uint8_t dev_addr) {
  logf("%d closed\n", dev_addr);
  // TODO
}


bool hcd_edpt_xfer(uint8_t rhport, uint8_t dev_addr, uint8_t ep_addr, uint8_t * buffer, uint16_t buflen) {
  uint8_t epnr = ep_addr & 0x7f;
  int channel = dwc_find_idle_channel();

  if (ep_addr & 0x80) { // IN
    logf(RED"\tHOST%d  <-   %d.%02x 0x%x/%d\n"DEFAULT, channel, dev_addr, ep_addr, (uint32_t)buffer, buflen);
    dwc_host_in(&dwc_state, channel, dev_addr, epnr, (uint32_t*)buffer, buflen, false);
  } else { // OUT
    logf(RED"\tHOST%d  ->   %d.%02x 0x%x/%d\n"DEFAULT, channel, dev_addr, ep_addr, (uint32_t)buffer, buflen);
    dwc_host_out(&dwc_state, channel, dev_addr, epnr, (uint32_t*)buffer, buflen, false);
  }
  return true;
}

static int dwc_find_idle_channel(void) {
  for (int i=0; i<8; i++) {
    struct dwc2_host_channel *chan = get_channel(i);

    if (chan->hcchar & BIT(31)) continue;
    if (chan->hcchar & BIT(30)) continue;
    return i;
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
    uint32_t framenr = *REG32(USB_HFNUM) & 0xffff;
    if (framenr % 10000 == 0) {
      //printf("SOF interval: %d\n", *REG32(ST_CLO) - last_sof);
    }
    last_sof = *REG32(ST_CLO);
  }

  if (interrupt_status & BIT(4)) {
    ack |= BIT(4);
    //logf("RX irq\n");
    //dumpreg(USB_GRXSTSP);
    uint32_t sts = *REG32(USB_GRXSTSP);
    int chan = sts & 0xf;
    int status = (sts >> 17) & 0xf;
    if (chan == debug_channel) {
      printf("  channel: %d\n", chan);
      printf("  byte count: %d\n", (sts >> 4) & 0x7ff);
      printf("  PID: %d\n", (sts >> 15) & 0x3);
      printf("  packet status: %d\n", status);
    }
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
      event_signal(&state->channels[chan].irq, false);
      ret = INT_RESCHEDULE;
    } else if (status == 3) {
      //logf("IN complete\n");
      struct dwc2_host_channel *channel = get_channel(chan);
      uint32_t hcchar = channel->hcchar;
      //uint32_t type = (hcchar >> 18) & 0x3;
      bool in = hcchar & BIT(15);
      uint8_t epnr = (hcchar >> 11) & 0xf;
      uint8_t devaddr = (hcchar >> 22) & 0x7f;
      uint32_t bytes = (sts >> 4) & 0x7ff;
      logf("hcd_event_xfer_complete(%d, 0x%x, %d, success, true)\n", devaddr, epnr | (in ? 0x80 : 0), bytes);
      hcd_event_xfer_complete(devaddr, epnr | (in ? 0x80 : 0), bytes, XFER_RESULT_SUCCESS, true);
    } else {
      puts("unhandled packet status on rx fifo");
      printf("  channel: %d\n", chan);
      printf("  byte count: %d\n", (sts >> 4) & 0x7ff);
      printf("  PID: %d\n", (sts >> 15) & 0x3);
      printf("  packet status: %d\n", status);
    }
  }

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
      logf("Port Connect Detected\n");
      hcd_event_device_attach(0, true);
    }

    if (hprt & BIT(3)) {
      //logf("Port Enable/Disable Change\n");
      //dumpreg(USB_HPRT);


      if (*REG32(USB_HPRT) & BIT(2)) {
        //logf("port enabled\n");
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
        if (i == debug_channel) {
          uint32_t now = *REG32(ST_CLO);
          printf(RED"channel %d wants attention after %d uSec\n"DEFAULT, i, now - state->channels[i].req_start);
        }
        struct dwc2_host_channel *chan = get_channel(i);
        uint32_t int_flags = chan->hcint;
        if (i == debug_channel) {
          dump_channel(i);
        }

        if (int_flags & BIT(4)) {
          logf("NAK on chan %d\n", i);
        }
        //printf("acking 0x%x\n", int_flags);
        chan->hcint = int_flags;

        uint32_t hcchar = chan->hcchar;
        uint32_t type = (hcchar >> 18) & 0x3;
        bool in = hcchar & BIT(15);
        uint8_t epnr = (hcchar >> 11) & 0xf;
        uint8_t devaddr = (hcchar >> 22) & 0x7f;
        uint32_t bytes = chan->hctsiz & 0x7ffff;

        if (int_flags & BIT(4)) {
          logf(RED"TODO, NAK\n"DEFAULT);
          //chan->hcchar = BIT(30);
        } else if (int_flags & BIT(7)) {
          logf("transaction error on channel %d\n", i);
          //chan->hcchar = BIT(30);
          logf("hcd_event_xfer_complete(%d, 0x%x, %d, fail, true)\n", devaddr, epnr | (in ? 0x80 : 0), bytes);
          hcd_event_xfer_complete(devaddr, epnr | (in ? 0x80 : 0), bytes, XFER_RESULT_FAILED, true);
        } else if ((type == 0) && (!in)) { // control
          logf("hcd_event_xfer_complete(%d, 0x%x, %d, success, true)\n", devaddr, epnr | (in ? 0x80 : 0), bytes);
          hcd_event_xfer_complete(devaddr, epnr | (in ? 0x80 : 0), bytes, XFER_RESULT_SUCCESS, true);
        }

        event_signal(&state->channels[i].irq, false);
        ret = INT_RESCHEDULE;
      }
    }
  }

  if (interrupt_status & BIT(28)) {
    ack |= BIT(28);
    logf("Connector ID Status Change\n");
  }

  if (interrupt_status & BIT(29)) {
    ack |= BIT(29);
    logf("Disconnect Detected Interrupt\n");
    hcd_event_device_remove(0, true);
  }

  if (interrupt_status & BIT(30)) {
    ack |= BIT(30);
    logf("Session Request/New Session Detected Interrupt\n");
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

static void dump_channel(int i) {
  struct dwc2_host_channel *chan = get_channel(i);
  uint32_t t;

  printf("dumping channel %d\n", i);
  printf("  HCCHAR%d: 0x%x\n", i, chan->hcchar);
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
  printf("  HCTSIZ%d: 0x%x\n", i, chan->hctsiz);
  printf("  HCDMA%d: 0x%x\n", i, chan->hcdma);
}

static void channel_wait_irq(dwc_host_state_t *state, int channel) {
  event_wait(&state->channels[channel].irq);
}

static void dwc_send_setup(dwc_host_state_t *state, int channel, int addr, int endpoint, setupData *setup, bool blocking) {
  struct dwc2_host_channel *chan = get_channel(channel);

  //thread_sleep(500);
  if (channel == debug_channel) {
    logf(RED"dwc_send_setup(0x%x, %d, %d, %d, 0x%x)\n"DEFAULT, (uint32_t)state, channel, addr, endpoint, (uint32_t)setup);
    dump_channel(channel);
  }
  assert((chan->hcchar & HCCHARn_ENABLE) == 0);

  state->channels[channel].req_start = *REG32(ST_CLO);

  chan->hctsiz = HCTSIZn_BYTES(8) | HCTSIZn_PACKET_COUNT(1) | HCTSIZn_PID(3);
  chan->hcsplt = 0;
  chan->hcdma = 0;
  chan->hcint = 0xffff;
  chan->hcintmsk = 0xffff;
  chan->hcchar = HCCHARn_MAX_PACKET_SIZE(8) | HCCHARn_ENDPOINT(endpoint) | HCCHARn_ADDR(addr) | (1<<20) | HCCHARn_OUT;
  chan->hcchar |= HCCHARn_ENABLE;
  //dumpreg(USB_GNPTXSTS); // 2c

  uint32_t *src = (uint32_t*)setup;
  uint32_t *dest = (uint32_t*)USB_DFIFO(channel);
  dest[0] = src[0];
  dest[0] = src[1];
  //dumpreg(USB_GNPTXSTS); // 2c

  if (blocking) {
    puts("waiting for irq");
    channel_wait_irq(state, channel);
    logf("SETUP done\n");
  }
}

static void dwc_host_in(dwc_host_state_t *state, int channel, int addr, int endpoint, uint32_t *buffer, uint32_t size, bool blocking) {
  struct dwc2_host_channel *chan = get_channel(channel);
  int max_packet_size = 64; // TODO, get it from hcd_edpt_open()
  int type = 0;
  int pid = 2;

  if (endpoint == 1) {
    type = 3;
    pid = 0;
    logf("type %d, mps %d, addr %d, pid %d, buffer 0x%x\n", type, max_packet_size, addr, pid, (uint32_t)buffer);
  }

  state->channels[channel].req_start = *REG32(ST_CLO);
  state->channels[channel].buffer = buffer;
  state->channels[channel].buffer_size = size;

  chan->hctsiz = HCTSIZn_BYTES(size) | HCTSIZn_PACKET_COUNT(1) | HCTSIZn_PID(pid);
  chan->hcsplt = 0;
  chan->hcint = 0xffff;
  chan->hcintmsk = 0xffff;
  chan->hcchar = HCCHARn_MAX_PACKET_SIZE(max_packet_size) | HCCHARn_ENDPOINT(endpoint) | HCCHARn_ADDR(addr) | HCCHARn_IN | (1<<20) | HCCHARn_ENABLE | (type << 18);

  if (blocking) {
    channel_wait_irq(state, channel);
    logf("IN done\n");
  }
}

static void dwc_host_out(dwc_host_state_t *state, int channel, int addr, int endpoint, uint32_t *buffer, uint32_t size, bool blocking) {
  struct dwc2_host_channel *chan = get_channel(channel);

  assert(size == 0);
  if (channel == debug_channel) {
    logf("size=%d\n", size);
    dump_channel(channel);
  }
  assert((chan->hcchar & HCCHARn_ENABLE) == 0);

  state->channels[channel].req_start = *REG32(ST_CLO);
  chan->hctsiz = HCTSIZn_BYTES(0) | HCTSIZn_PACKET_COUNT(1) | HCTSIZn_PID(3);
  chan->hcsplt = 0;
  chan->hcint = 0xffff;
  chan->hcintmsk = 0xffff;
  chan->hcchar = HCCHARn_MAX_PACKET_SIZE(8) | HCCHARn_ENDPOINT(endpoint) | HCCHARn_ADDR(addr) | (1<<20) | HCCHARn_OUT | HCCHARn_ENABLE;

  if (blocking) {
    channel_wait_irq(state, channel);
    logf("0-length OUT done\n");
  }
}

static void control_in(dwc_host_state_t *state, int channel, int addr, int endpoint, setupData *setup, uint32_t *buffer) {
  dwc_send_setup(state, channel, addr, endpoint, setup, true);

  dwc_host_in(state, channel, addr, endpoint, buffer, setup->wLength, true);

  dwc_host_out(state, channel, addr, endpoint, NULL, 0, true);
}

int dwc_addr_assigner(void *arg) {
  //dwc_host_state_t *state = arg;
  while (true) {
    //struct list_node *next = queue_pop(&state->portsPendingAddress);
  }
  return 0;
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
  }

  if (device_mode) {
  }
}

bool hcd_init(uint8_t rhport) {
  lan_run(0);
  thread_sleep(10);
  lan_run(1);
  logf("\n");
  return true;
}

void hcd_int_enable (uint8_t rhport) {
  logf("\n");
}

#define PRINTREG(x) t = *REG32(x); printf( #x ": 0x%x\n", t)
static void dwc2_init_hook(uint level) {
  puts("dwc2 init hook");
  uint32_t t;

  dwc_dump_all_state();

  *REG32(USB_GINTMSK) = BIT(1) | BIT(2) | /*BIT(3) |*/ BIT(4) | BIT(24) | BIT(25) | BIT(28) | BIT(29) | BIT(30);
  *REG32(USB_GINTSTS) = BIT(1) | BIT(11) | BIT(12) | BIT(15); // clear some interrupts
  *REG32(USB_HAINTMSK) = 0xffff;

  for (int i=0; i<MAX_DEVICES; i++) {
    dwc_state.devices[i] = NULL;
  }
  dwc_state.nextDevice = 0;
  dwc_state.nextAddress = 1;
  for (int i=0; i<MAX_CHANNELS; i++) {
    event_init(&dwc_state.channels[i].irq, false, EVENT_FLAG_AUTOUNSIGNAL);
  }

  logf("queue init\n");
  queue_init(&dwc_state.portsPendingAddress);

  dwc_state.addressAssigner = thread_create("dwc_addr", &dwc_addr_assigner, &dwc_state, DEFAULT_PRIORITY, 0);
  logf("thread resume\n");
  //thread_resume(dwc_state.addressAssigner);

  register_int_handler(DWC_IRQ, (int_handler)dwc_irq, &dwc_state);
  logf("int unmask\n");
  unmask_interrupt(DWC_IRQ);




  PRINTREG(USB_HAINT);    // 414
  PRINTREG(USB_HAINTMSK); // 418
  dumpreg(USB_GAHBCFG);
  //*REG32(USB_GAHBCFG) = BIT(0) | BIT(5);
  dumpreg(USB_GAHBCFG);

  for (int i=0; i<2; i++) {
    struct dwc2_host_channel *chan = (struct dwc2_host_channel *)(USB_BASE + 0x500 + (i * 0x20));
    printf("HCCHAR%d: 0x%x\n", i, chan->hcchar);
    printf("HCSPLT%d: 0x%x\n", i, chan->hcsplt);
    printf("HCTSIZ%d: 0x%x\n", i, chan->hctsiz);
    printf("HCDMA%d: 0x%x\n", i, chan->hcdma);
  }

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
    dump_channel(0);
  }
}

LK_INIT_HOOK(dwc2, &dwc2_init_hook, LK_INIT_LEVEL_PLATFORM);
