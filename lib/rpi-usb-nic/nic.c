#include "tusb.h"
#include "host/usbh_classdriver.h"
#include <kernel/timer.h>
#include <lib/hexdump.h>
#include <lib/rpi-usb-nic/nic.h>
#include <linked-list-fifo.h>
#include <lk/console_cmd.h>
#include <lwip/dhcp.h>
#include <lwip/netif.h>
#include <netif/etharp.h>
#include <platform/bcm28xx/otp.h>
#if !defined(ARCH_VPU)
#include <platform/bcm28xx/inter-arch.h>
#endif

#define BIT(b) (1 << b)

#define REG_MAC_CR 0x100
#define REG_ADDRH 0x104
#define REG_ADDRL 0x108
#define REG_INT_EP_CTL 0x68

#define REG_MII_ACCESS  0x114

#define CSI "\x1b["
#define RED     CSI"31m"
#define GREEN   CSI"32m"
#define DEFAULT CSI"39m"

typedef struct {
  struct netif netif;
  uint8_t daddr;
  uint8_t *tx_buffer;
  tuh_xfer_t tx_xfer;
  mutex_t usb_lock;
  bool tx_busy;
  thread_t *rx_thread;
  thread_t *tx_thread;
  event_t rx_running;
  fifo_t tx_queue;
} nic_state_t;

typedef struct {
  struct list_node node;
  struct pbuf *p;
} queued_packet_t;

typedef struct {
  uint32_t tx_good;
  uint32_t tx_pause;
  uint32_t tx_single_col;
  uint32_t tx_multi_col;

  uint32_t tx_ex_col;
  uint32_t tx_late_col;
  uint32_t tx_buff_under;
  uint32_t tx_exc_def;

  uint32_t tx_carrier_error;
  uint32_t tx_bad_frames;
} tx_stats_t;

static uint32_t interrupt_buffer;
static timer_t nic_poll;
//static uint8_t rx_buffer[4096] __attribute__((aligned(16)));
static tuh_xfer_t rx_xfer;

static nic_state_t *ns = NULL;

static int cmd_dump_regs(int argc, const console_cmd_args *argv);
static int cmd_stats(int argc, const console_cmd_args *argv);
static int nic_rx_thread(void *arg);
static int nic_start_thread(void *arg);
static int nic_tx_thread(void *arg);
static void get_stats(tx_stats_t *tx_stats);
static void nic_int_cb(tuh_xfer_t *xfer);
static void nic_int_cb(tuh_xfer_t *xfer);

STATIC_COMMAND_START
STATIC_COMMAND("nic_dump_regs", "dump all nic control regs", &cmd_dump_regs)
STATIC_COMMAND("nic_stats", "show hw stat counters", &cmd_stats)
STATIC_COMMAND_END(nic);

static uint32_t register_read(nic_state_t *state, uint16_t reg) {
  thread_sleep(100);
  xfer_result_t result = 10;
  uint32_t read_buf;
  bool status;
  tusb_control_request_t const request = {
    .bmRequestType_bit = {
      .recipient = TUSB_REQ_RCPT_DEVICE,
      .type      = TUSB_REQ_TYPE_VENDOR,
      .direction = TUSB_DIR_IN,
    },
    .bRequest = 0xa1,
    .wValue = 0,
    .wIndex = tu_htole16(reg),
    .wLength = tu_htole16(4),
  };
  tuh_xfer_t xfer = {
    .daddr = state->daddr,
    .ep_addr = 0,
    .setup = &request,
    .buffer = (void*)&read_buf,
    .complete_cb = 0,
    .user_data = (uintptr_t)&result,
  };

retry2:
  //mutex_acquire(&state->usb_lock);
  status = tuh_control_xfer(&xfer);
  //mutex_release(&state->usb_lock);
  if (!status) {
    puts("retrying");
    thread_sleep(10000);
    goto retry2;
  }

  if (((reg != REG_MII_ACCESS) && (reg != 0x118)) || (!status) || (result != XFER_RESULT_SUCCESS)) {
    printf(GREEN"0x%03x -> 0x%08x, %d %d\n"DEFAULT, reg, read_buf, status, result);
  }
  assert(status);
  assert(result == XFER_RESULT_SUCCESS);
  return read_buf;
}

static int cmd_dump_regs(int argc, const console_cmd_args *argv) {
  for (int i=0; i<0x70; i += 4) {
    register_read(ns, i);
  }
  for (int i=0x100; i<0x134; i += 4) {
    register_read(ns, i);
  }
  return 0;
}

static int cmd_stats(int argc, const console_cmd_args *argv) {
  tx_stats_t tx;
  memset(&tx, 0x55, sizeof(tx));
  get_stats(&tx);
  printf("&tx: 0x%p %d\n", &tx, (uint32_t)sizeof(tx));
  hexdump_ram(&tx, (uintptr_t)&tx, sizeof(tx)+16);
  printf("bad frames: %d\n", tx.tx_bad_frames);
  return 0;
}

static void register_write(nic_state_t *state, uint16_t reg, uint32_t value) {
  thread_sleep(100);
  bool status;
  xfer_result_t result = XFER_RESULT_INVALID;
  tusb_control_request_t const request = {
    .bmRequestType_bit = {
      .recipient = TUSB_REQ_RCPT_DEVICE,
      .type      = TUSB_REQ_TYPE_VENDOR,
      .direction = TUSB_DIR_OUT,
    },
    .bRequest = 0xa0,
    .wValue = 0,
    .wIndex = tu_htole16(reg),
    .wLength = tu_htole16(4),
  };
  tuh_xfer_t xfer = {
    .daddr = state->daddr,
    .ep_addr = 0,
    .setup = &request,
    .buffer = (void*)&value,
    .complete_cb = 0,
    .user_data = (uintptr_t)&result,
  };

  //mutex_acquire(&state->usb_lock);
retry:
  status = tuh_control_xfer(&xfer);
  if (!status) {
    puts("retrying");
    thread_sleep(100);
    goto retry;
  }
  //mutex_release(&state->usb_lock);
  printf(GREEN"0x%03x <- 0x%08x, %d %d\n"DEFAULT, reg, value, status, result);
  assert(status);
  assert(result == XFER_RESULT_SUCCESS);
}

static void phy_wait_not_busy(nic_state_t *state) {
  while (register_read(state, REG_MII_ACCESS) & 1) {}
}

static void phy_write(nic_state_t *state, uint8_t reg, uint16_t value) {
  //puts("phy write start");
  phy_wait_not_busy(state);
  register_write(state, 0x118, value);
  register_write(state, REG_MII_ACCESS, (1<<11) | (reg << 6) | BIT(1) | BIT(0));
  phy_wait_not_busy(state);
  printf(GREEN"PHY(%d) <- 0x%x\n"DEFAULT, reg, value);
}

static uint16_t phy_read(nic_state_t *state, uint8_t reg) {
  //printf("phy read(%d) start\n", reg);
  phy_wait_not_busy(state);
  register_write(state, REG_MII_ACCESS, (1<<11) | (reg << 6) | BIT(0));
  phy_wait_not_busy(state);
  uint16_t ret = register_read(state, 0x118);
  printf(GREEN"PHY(%d) -> 0x%x\n"DEFAULT, reg, ret);
  return ret;
}

static enum handler_return nic_poll_again(timer_t *t, unsigned int now,  void *user) {
  nic_state_t *state = user;
  usbh_edpt_xfer_with_callback(state->daddr, 0x83, (void*)&interrupt_buffer, 4, nic_int_cb, 0);

  return INT_NO_RESCHEDULE;
}

static void rx_cb(tuh_xfer_t *xfer) {
  struct pbuf *p = (struct pbuf*)xfer->user_data;
  if (xfer->actual_len == 0) {
    pbuf_free(p);
    event_signal(&ns->rx_running, true);
    return;
  }
  uint8_t *rx_buffer = p->payload;

  uint32_t status_word = *((uint32_t*)rx_buffer);
  //if (status_word & BIT(10)) return; // multicast

#if 0
  printf("rx cb %d / 0x%x, buf at 0x%x\n", xfer->actual_len, xfer->actual_len, (uint32_t)rx_buffer);
  hexdump_ram(rx_buffer, (uint32_t)rx_buffer, xfer->actual_len);

  uint32_t packet_length = (status_word >> 16) & 0x3fff;
  // 5 10 13
  if (status_word & BIT(5)) puts("  5 frame type");
  if (status_word & BIT(10)) puts("  10 multicast");
  if (status_word & BIT(13)) puts("  13 broadcast");
  printf("  16:29 length: %d / 0x%x\n", packet_length, packet_length);
  if (status_word & BIT(30)) puts("  30 filtering fail");
#endif

  // remove handled bits, and print unhandled bits
  status_word &= ~(BIT(5) | BIT(10) | BIT(13) | BIT(30));
  status_word &= ~(0x3fff << 16);
  if (status_word) {
    printf("status word 0x%x\n", status_word);
  }

  pbuf_header(p, -4);

  ns->netif.input(p, &ns->netif);

  event_signal(&ns->rx_running, true);
}

static void start_rx(nic_state_t *state) {
  //puts("rx starting");
  struct pbuf *p = pbuf_alloc(PBUF_RAW, 1600, PBUF_POOL);
  assert(p->len == 1600);
  rx_xfer.daddr = state->daddr;
  rx_xfer.ep_addr = 0x81;
  rx_xfer.buffer = p->payload;
  rx_xfer.buflen = 4096;
  rx_xfer.complete_cb = rx_cb;
  rx_xfer.user_data = (uintptr_t)p;

  tuh_edpt_xfer(&rx_xfer);
}

static void get_stats(tx_stats_t *tx_stats) {
  tusb_control_request_t const request = {
    .bmRequestType_bit = {
      .recipient = TUSB_REQ_RCPT_DEVICE,
      .type      = TUSB_REQ_TYPE_VENDOR,
      .direction = TUSB_DIR_IN,
    },
    .bRequest = 0xa2,
    .wValue = 0,
    .wIndex = 1,
    .wLength = 0x28,
  };
  tuh_xfer_t xfer = {
    .daddr = ns->daddr,
    .ep_addr = 0,
    .setup = &request,
    .buffer = (uint8_t*)tx_stats,
    .complete_cb = 0,
    .user_data = 0,
  };

  mutex_acquire(&ns->usb_lock);
  tuh_control_xfer(&xfer);
  mutex_release(&ns->usb_lock);
}

static void nic_int_cb(tuh_xfer_t *xfer) {
  nic_state_t *state = ns;

  if (interrupt_buffer & BIT(15)) {
    uint16_t phy_irq = phy_read(state, 29);
    printf("PHY irq: 0x%x\n", phy_irq);
    if (phy_irq & BIT(1)) {
      puts("  Auto-Negotiation Page Received");
    }
    if (phy_irq & BIT(3)) {
      puts("  Auto-Negotiation LP Acknowledge");
    }
    if (phy_irq & BIT(4)) {
      puts("  Link Down");
    }
    if (phy_irq & BIT(6)) {
      puts("  Auto-Negotiation complete");
      register_read(state, 0x14);
      netif_set_link_up(&state->netif);
      dhcp_start(&state->netif);
    }
    if (phy_irq & BIT(7)) {
      puts("  ENERGYON generated");
    }
    // TODO call netif_set_link_down and netif_set_link_up
  }

  //if (interrupt_buffer & BIT(18)) {
  //  interrupt_buffer &= ~BIT(18);
  //  puts("RX fifo has packet");
    //start_rx(state);
  //}
  if (interrupt_buffer) printf("int %x\n", interrupt_buffer);

  timer_set_oneshot(&nic_poll, 100, nic_poll_again, state);
}

static void phy_dump(nic_state_t *state) {
  //uint16_t basic_control = phy_read(0);

  uint16_t basic_status = phy_read(state, 1);
  printf("basic Status: 0x%x\n", basic_status);
  if (basic_status & BIT(2)) puts("  link up");
  if (basic_status & BIT(3)) puts("  auto capable");

  uint16_t phy_special = phy_read(state, 31);
  printf("PHY special: 0x%x\n", phy_special);
  if (phy_special & BIT(12)) puts("  auto done");
}

static err_t ethernetif_output(struct netif *netif, struct pbuf *p, const ip4_addr_t *ipaddr) {
  return etharp_output(netif, p, ipaddr);
}

static void nic_tx_complete(tuh_xfer_t *xfer) {
  //puts("tx done");
  ns->tx_busy = false;
  //cmd_stats(0, NULL);
}

static err_t nic_tx_queue(struct netif *netif, struct pbuf *p) {
  queued_packet_t *qp = malloc(sizeof(queued_packet_t));
  qp->p = p;
  //printf("tx queued, %p %p %d/%d\n", qp, p, p->len, p->tot_len);
  fifo_push(&ns->tx_queue, &qp->node, true);
  return ERR_OK;
}

static err_t nic_tx(struct pbuf *p) {
  bool status;
  //printf("tx attempt, %p %d/%d bytes, %x, caller %p\n", p, p->len, p->tot_len, (uint32_t)ns->tx_buffer, __GET_CALLER());
  assert(p->len == p->tot_len);

  if (ns->tx_busy) return ERR_OK; // TODO, need a tx queue

  uint32_t *commands = (uint32_t*)ns->tx_buffer;
  commands[0] = p->len | BIT(12) | BIT(13);
  commands[1] = p->len; // | BIT(14);
  //commands[2] = 0;

  memcpy(ns->tx_buffer + 8, p->payload, p->len);

  //hexdump_ram(ns->tx_buffer, 0, p->len + 8);

  ns->tx_xfer.daddr = ns->daddr;
  ns->tx_xfer.ep_addr = 0x2;
  ns->tx_xfer.buffer = ns->tx_buffer;
  ns->tx_xfer.buflen = p->len + 8;
  ns->tx_xfer.complete_cb = nic_tx_complete;
  ns->tx_xfer.user_data = 0;

  pbuf_free(p);

retry3:
  status = tuh_edpt_xfer(&ns->tx_xfer);
  if (!status) {
    puts("tx error");
    thread_sleep(100);
    goto retry3;
  }

  return ERR_OK;
}

static void nic_status_cb(struct netif *netif) {
  printf("status cb, flags: 0x%x\n", netif->flags);
  if (netif_is_up(netif)) {
    const struct dhcp *d = netif_dhcp_data(netif);
    if (d) {
      printf("filename: %s\n", d->boot_file_name);
      printf("dhcp server: %s\n", ipaddr_ntoa(&d->server_ip_addr));
      printf("my ip: %s\n", ipaddr_ntoa(&netif->ip_addr));
      printf("next server: %s\n", ipaddr_ntoa(&d->offered_si_addr));
    }
  }
}

static void nic_ext_cb(struct netif* netif, netif_nsc_reason_t reason, const netif_ext_callback_args_t* args) {
  puts("nic new cb");
  nic_status_cb(netif);
}

NETIF_DECLARE_EXT_CALLBACK(nic_ext_cb_ctx);

static signed char nic_init(struct netif *netif) {
  nic_state_t *state = (nic_state_t*)netif;
#ifdef ARCH_VPU
  uint32_t serial = otp_read(28);
#else
  uint32_t serial = hw_serial;
#endif

  //netif->status_callback = nic_status_cb;
  netif_add_ext_callback(&nic_ext_cb_ctx, nic_ext_cb);

  netif->hwaddr_len = 6;
  netif->hwaddr[0] = 0xb8;
  netif->hwaddr[1] = 0x27;
  netif->hwaddr[2] = 0xeb;
  netif->hwaddr[3] = (serial >> 16) & 0xff;
  netif->hwaddr[4] = (serial >> 8) & 0xff;
  netif->hwaddr[5] = (serial >> 0) & 0xff;

  netif->flags |= NETIF_FLAG_ETHARP;
  netif_set_up(netif);

  netif->name[0] = 'e';
  netif->name[1] = 'n';

  netif->mtu = 1500;

  netif->output = ethernetif_output;
  netif->linkoutput = nic_tx_queue;

  state->tx_buffer = memalign(16, 1600);

  register_write(state, REG_ADDRH, 0xb827); // upper 16bits of mac
  register_write(state, REG_ADDRL, (0xeb << 24) | (serial & 0xffffff)); // lower 24bits of mac

  return ERR_OK;
}

void nic_start(uint8_t daddr) {
  uintptr_t hack = daddr;
  thread_t *t = thread_create("nic init thread", nic_start_thread, (void*)hack, DEFAULT_PRIORITY, DEFAULT_STACK_SIZE);
  thread_detach_and_resume(t);
}

static int nic_start_thread(void *arg) {
  uintptr_t hack = (uintptr_t)arg;
  uint8_t daddr = (uint8_t)hack;
  printf("probing NIC at %d\n", daddr);

  nic_state_t *state = malloc(sizeof(nic_state_t));
  state->daddr = daddr;
  state->tx_busy = false;
  mutex_init(&state->usb_lock);
  event_init(&state->rx_running, false, EVENT_FLAG_AUTOUNSIGNAL);
  fifo_init(&state->tx_queue);

  thread_sleep(5000);

  uint32_t ident = register_read(state, 0);
  if (ident != 0xec000002) {
    printf("unexpected ident: 0x%x\n", ident);
    free(state);
    return 0;
  }

  ns = state;

  register_write(state, 0x130, BIT(16));
  register_write(state, REG_MAC_CR, BIT(31) | BIT(20) | BIT(19) | BIT(3) | BIT(2));
  register_write(state, 0x10, BIT(2));
  register_write(state, 0x14, BIT(12)); // bulk-in should NAK when it has nothing
  register_write(state, REG_INT_EP_CTL, BIT(15) | BIT(14)); // irq mask

  phy_write(state, 0, BIT(15)); // soft reset
  thread_sleep(100);

  phy_write(state, 0, BIT(12) | BIT(9)); // restart auto-negotiation and enable auto neg
  phy_write(state, 27, BIT(14)); // enable auto crossover

  phy_write(state, 30, BIT(3) | BIT(4) | BIT(6)); // PHY irq mask

  //thread_sleep(10000);

#if 0
  register_read(daddr, 8);
  register_read(daddr, 0xc);
  register_read(daddr, 0x10);
  register_read(daddr, 0x14);
  for (int reg = 0x100; reg <= 0x130; reg += 4) {
    register_read(daddr, reg);
  }

  puts("phy regs");
  for (int i=0; i<32; i++) {
    uint16_t val = phy_read(i);
    printf("%d = 0x%x\n", i, val);
  }

  phy_dump();
#endif

  uint8_t temp_buf[512];

  uint8_t status = tuh_descriptor_get_configuration_sync(daddr, 0, temp_buf, sizeof(temp_buf));
  if (status == XFER_RESULT_SUCCESS) {
    uint8_t *t = temp_buf;
    int sizelimit = 512;
    while (t < &temp_buf[sizelimit]) {
      switch (t[1]) { // type
      case 2: // configuration
        {
          tusb_desc_configuration_t *cfg = (tusb_desc_configuration_t*)t;
          //printf("interfaces: %d\n", cfg->bNumInterfaces);
          //printf("total length: %d\n", cfg->wTotalLength);
          assert(cfg->wTotalLength < sizelimit);
          sizelimit = cfg->wTotalLength;
        }
        break;
      case 4: // interface
        break;
      case 5: // endpoint
        {
          tusb_desc_endpoint_t *ep = (tusb_desc_endpoint_t*)t;
          //printf("length %d\n", ep->bLength);
          //printf("type 0x%x\n", ep->bDescriptorType);
          //printf("addr 0x%x\n", ep->bEndpointAddress);
          tuh_edpt_open(daddr, ep);
        }
        break;
      }
      //printf("size: %d\n", t[0]);
      //printf("type: %d\n", t[1]);
      t += t[0];
    }
  } else {
    //printf("failed to read config descriptor: %d\n", status);
    tusb_desc_endpoint_t ep = {
      .bEndpointAddress = 0x81,
      .bmAttributes = {
        .xfer = 2, // bulk
      },
      .wMaxPacketSize = 512,
    };
    tuh_edpt_open(daddr, &ep);
    ep.bEndpointAddress = 0x02;
    tuh_edpt_open(daddr, &ep);
    ep.bEndpointAddress = 0x83;
    ep.bmAttributes.xfer = 3; // interrupt
    tuh_edpt_open(daddr, &ep);
  }
  //puts("endpoints opened");
  timer_initialize(&nic_poll);

  netif_add(&state->netif, NULL, NULL, NULL, state, nic_init, ethernet_input);

  nic_poll_again(NULL, 0, state);
  state->rx_thread = thread_create("nic rx thread", nic_rx_thread, state, DEFAULT_PRIORITY, DEFAULT_STACK_SIZE);
  thread_resume(state->rx_thread);
  state->tx_thread = thread_create("nic tx thread", nic_tx_thread, state, DEFAULT_PRIORITY, DEFAULT_STACK_SIZE);
  thread_resume(state->tx_thread);

  netif_set_default(&state->netif);

  return 0;
}

static int nic_rx_thread(void *arg) {
  nic_state_t *state = arg;
  while (true) {
    start_rx(state);
    event_wait(&state->rx_running);
    //thread_sleep(10);
  }
  return 0;
}

static int nic_tx_thread(void *arg) {
  nic_state_t *state = arg;
  while (true) {
    queued_packet_t *qp = (queued_packet_t*)fifo_pop(&state->tx_queue);
    nic_tx(qp->p);
    free(qp);
  }
  return 0;
}
