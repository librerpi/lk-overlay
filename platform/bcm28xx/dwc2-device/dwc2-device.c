#include <app.h>
#include <platform/bcm28xx/udelay.h>
#include <assert.h>
#include <stdlib.h>
#include <lk/reg.h>
#include <platform/bcm28xx/dwc2.h>
#include <platform/bcm28xx/power.h>
#include <platform/interrupts.h>
#include <stdio.h>
#include <lk/trace.h>

#define LOCAL_TRACE 0

#include "callbacks.h"

#define CSI "\x1b["
#define RED     CSI"31m"
#define GREEN   CSI"32m"
#define DEFAULT CSI"39m"

#define BIT(n) (1<<n)
#define GET_IN(epNr) ((endpoint_control*) ((USB_BASE + 0x0900) + (epNr) * 0x20))
#define GET_OUT(epNr) ((endpoint_control*) ((USB_BASE + 0x0b00) + (epNr) * 0x20))

typedef struct {
  volatile uint32_t control;    //  0
  uint32_t pad1;                //  4
  volatile uint32_t interrupt;  //  8
  uint32_t pad2;                //  c
  volatile uint32_t size;       // 10
  volatile uint32_t dma;        // 14
  volatile uint32_t fifo_status;// 18   number of 32bit words that are free in the fifo
} endpoint_control;

typedef struct {
  packet_queue_t *packet_queue_head;
  packet_queue_t *packet_queue_tail;
} endpoint_t;

typedef struct {
  endpoint_t in[3];
} dwc_state_t;

static dwc_state_t state;
static uint8_t packet_buffer[4096];

void ep_write_in(int epNr);
static void dump_endpoint(endpoint_control *ep, bool in);

void dwc2_out_cb(packet_queue_t *pkt) {
  puts("nop");
  endpoint_control *ep_out = GET_OUT(0);
  ep_out->control |= (1<<31) | // enable OUT0
      (1<<26); // clear NAK
}

void dwc2_out_cb_free(packet_queue_t *pkt) {
  free(pkt->payload);
  //puts("nop");
  endpoint_control *ep_out = GET_OUT(0);
  ep_out->control |= (1<<31) | // enable OUT0
      (1<<26); // clear NAK
}

void dwc2_in_cb(packet_queue_t *pkt) {
  puts("in done");
}

void dwc2_ep_queue_in(int epNr, const void *buffer, int bytes, void (*cb)(packet_queue_t *)) {
  packet_queue_t *pkt = malloc(sizeof(packet_queue_t));
  pkt->payload = buffer;
  pkt->payload_size = bytes;
  pkt->start = 0;
  pkt->next = NULL;
  pkt->cb = cb;
  pkt->has_0byte_tail = (bytes % 64) == 0;
  if (epNr > 0) pkt->has_0byte_tail = false;

  int do_tx = false;
  if (state.in[epNr].packet_queue_head == NULL) {
    // no pending packets, can tx right away
    LTRACEF("instant tx on ep %d\n", epNr);
    do_tx = true;
    state.in[epNr].packet_queue_head = pkt;
  }
  if (state.in[epNr].packet_queue_tail) {
    state.in[epNr].packet_queue_tail->next = pkt;
  }
  state.in[epNr].packet_queue_tail = pkt;
  if (do_tx) {
    ep_write_in(epNr);
  }
}

static enum handler_return dwc_irq(dwc_state_t *state) {
  uint32_t interrupt_status = *REG32(USB_GINTSTS);
  uint32_t mask = *REG32(USB_GINTMSK);
  uint32_t active_bits = interrupt_status & mask;
  LTRACEF("dwc_irq() sts = 0x%x, active 0x%x\n", interrupt_status, active_bits);

  if (interrupt_status & BIT(11)) {
    puts("USB Suspend");
    *REG32(USB_GINTSTS) = BIT(11);
  }

  if (interrupt_status & BIT(12)) {
    puts(GREEN"USB reset"DEFAULT);
    usb_reset();
    *REG32(USB_GINTSTS) = BIT(12);
  }

  if (interrupt_status & BIT(13)) {
    printf(GREEN"Speed Enumeration done, running at ");
    uint32_t sts = *REG32(USB_DSTS);
    switch ((sts >> 1) & 3) {
    case 0:
      puts("HS");
      break;
    case 1:
      puts("FS 30/60MHz");
      break;
    case 2:
      puts("LS");
      break;
    case 3:
      puts("FS 48MHz");
      break;
    }
    printf(DEFAULT);
    *REG32(USB_GINTSTS) = BIT(13);
  }

  int loops = 0;
  while (interrupt_status & BIT(4)) {
    //puts("rx fifo non-empty");
    uint32_t receive_status = *REG32(USB_GRXSTSP);
    int packet_status = (receive_status >> 17) & 0xf;
    int packet_size = (receive_status >> 4) & 0x7ff;
    int epNr = receive_status & 0xf;

    int words = ROUNDUP(packet_size,4) / 4;
    uint32_t *dest = (uint32_t*)packet_buffer;
    for (int i=0; i<words; i++) {
      dest[i] = *REG32(USB_DFIFO0);
    }
    if (packet_status == 2) {
      //printf("received %d bytes of BULK packet on EP %02x\n", packet_size, epNr);
      bulk_out_received(epNr, packet_buffer, packet_size);
    } else if (packet_status == 3) {
      printf("received %d bytes of IN completed packet on EP %02x\n", packet_size, epNr);
    } else if (packet_status == 4) {
      printf("received %d bytes of SETUP packet on EP %02x\n", packet_size, epNr);
      received_setup_packet(packet_buffer);
    } else if (packet_status == 6) {
      printf("received %d bytes of SETUP data packet on EP %02x\n", packet_size, epNr);
    } else {
      printf("received %d bytes of %d packet on EP %02x\n", packet_size, packet_status, epNr);
    }

    interrupt_status = *REG32(USB_GINTSTS);
    loops++;
  }
  if (loops) printf("serviced %d events in while loop\n", loops);
  uint32_t daint = *REG32(USB_DAINT);
  endpoint_control *ep = 0;
  while (daint != 0) {
    printf("DAINT: 0x%x\n", daint);
    for (int i=0; i<16; i++) {
      if ((daint >> i) & 1) {
        printf("IN %d irq\n", i);
        ep = GET_IN(i);
        //dump_endpoint(ep, true);
        uint32_t irq = ep->interrupt;
        //printf("acking irq bits 0x%x\n", irq);
        ep->interrupt = irq;
        if (irq & 1) {
          ep_write_in(i);
        }
      }
    }
    for (int i=16; i<32; i++) {
      if ((daint >> i) & 1) {
        ep = (endpoint_control*)((USB_BASE + 0x0b00) + (i-16) * 0x20);
        LTRACEF("OUT %d irq\n", i - 16);
        if (LOCAL_TRACE) dump_endpoint(ep, false);
        uint32_t irq = ep->interrupt;
        LTRACEF("acking OUT %d irq bits 0x%x\n", i-16, irq);
        ep->interrupt = irq;
        if (1 == (i - 16)) {
          GET_OUT(1)->control = BIT(31) | BIT(26) | (2<<18) | BIT(15) | 64;
        }
      }
    }
    daint = *REG32(USB_DAINT);
  }

  return INT_NO_RESCHEDULE;
}

static void dwc_start(void) {
  puts("soft resetting");
  // core soft reset
  *REG32(USB_GRSTCTL) = BIT(0);
  while ((*REG32(USB_GRSTCTL) & BIT(0)) != 0) {}

  puts("hclk soft resetting");
  // hclk soft reset
  *REG32(USB_GRSTCTL) = BIT(1);
  while ((*REG32(USB_GRSTCTL) & BIT(1)) != 0) {}

  // flush tx fifo 1
  *REG32(USB_GRSTCTL) = 0x420;
  while ((*REG32(USB_GRSTCTL) & 0x20) != 0) {}

  // flush rx fifo
  *REG32(USB_GRSTCTL) = 0x10;
  while ((*REG32(USB_GRSTCTL) & 0x10) != 0) {}

  *REG32(USB_GAHBCFG) = BIT(0); // global irq enable
}

#define dumpreg(reg) { t = *REG32(reg); printf(#reg":\t 0x%x\n", t); }
static void dwc2d_init(const struct app_descriptor *app) {
  uint32_t t;

  power_up_usb();

  dwc_start();

  dumpreg(USB_GHWCFG3);

  *REG32(USB_GINTMSK) =
    BIT(19) | BIT(18) | BIT(17) |
    BIT(13) | BIT(12) | BIT(11) | BIT(4)
    | BIT(2)
    ;
  *REG32(USB_DIEPMSK) = (1<<4) | (1<<3) | (1<<0);
  *REG32(USB_DOEPMSK) = BIT(8) | BIT(4) | BIT(3) | BIT(1) | BIT(0);

  register_int_handler(DWC_IRQ, dwc_irq, &state);
  unmask_interrupt(DWC_IRQ);

  printf(GREEN"dwc device init done\n"DEFAULT);
}

void ep_write_in(int epNr) {
  endpoint_control *ep = GET_IN(epNr);
  printf("control: 0x%x\n", ep->control);
  while (ep->control & BIT(31))  {}
  LTRACEF("ready\n");
  int maxPacketSize = 64; // TODO
  packet_queue_t *pkt = state.in[epNr].packet_queue_head;
  assert(pkt);
  LTRACEF("packet(%x): 0x%x+(0x%x/0x%x)\n", (uint32_t)pkt, (uint32_t)pkt->payload, pkt->start, pkt->payload_size);
  int bytes = pkt->payload_size - pkt->start;
  if ((bytes <= 0) && !pkt->has_0byte_tail) {
    printf("IN %d packet fully sent\n", epNr);
    pkt->cb(pkt);
    packet_queue_t *next = pkt->next;
    state.in[epNr].packet_queue_head = next;
    if (state.in[epNr].packet_queue_tail == pkt) state.in[epNr].packet_queue_tail = NULL;
    free(pkt);
    if (next) return ep_write_in(epNr);
    return;
  }
  assert(bytes >= 0);
  int fullPackets = bytes / maxPacketSize;
  int partialPacketSize = bytes - (fullPackets*maxPacketSize);
  int packets = fullPackets + ((partialPacketSize==0) ? 0 : 1);
  int words = ROUNDUP(bytes,4)/4;
  LTRACEF("sending %d full packets and a %d byte partial, %d total, %d words\n", fullPackets, partialPacketSize, packets, words);
  if (bytes == 0) {
    pkt->has_0byte_tail = false;
    printf("sending tail\n");
  }
  if (epNr == 0) {
    packets = 1;
    bytes = MIN(maxPacketSize, bytes);
    printf("capped to 1 packet of %d bytes\n", bytes);
  }
  uint32_t x = (3 << 29) | (packets<<19) | bytes;

  ep->size = x;
  int type = 0; // control
  int fifoNr = epNr;
  int maxPacket = 0;

  if (epNr == 2) {
    type = 2; // bulk
    maxPacket = 64;
  }

  ep->control = EP_ENABLE | EP_CLEAR_NAK |
    (fifoNr << 22) |
    EP_TYPE(type) |
    EP_ACTIVE |
    EP_MAX_PACKET(maxPacket);
  //dump_endpoint(ep, true);
  //ack_ep(ep);

  uint32_t *packet = (uint32_t*)(pkt->payload + pkt->start);
  int bytes_sent = 0;
  for (int i = 0; i < MIN(maxPacketSize/4, words); i++) {
    *REG32(USB_DFIFO0 + (0x1000 * epNr)) = packet[i];
    //*REG32(USB_DFIFO0) = packet[i];
    LTRACEF("%d: posted 0x%08x to fifo\n", i, packet[i]);
    bytes_sent += 4;
  }
  pkt->start += bytes_sent;
  printf("%d bytes sent, start now %d\n", bytes_sent, pkt->start);
}

void set_configuration(void) {
  dwc2_ep_queue_in(0, NULL, 0, &dwc2_in_cb);
  GET_OUT(1)->control = BIT(31) | BIT(26) | (2<<18) | BIT(15) | 64;
  GET_IN(2)->control = (2 << 18) | BIT(15);
}

void prepare_out(int epNr, int packets, int bytes) {
  endpoint_control *ep1 = GET_OUT(epNr);
  dump_endpoint(ep1, false);

  ep1->control = (ep1->control & (~BIT(31))) | BIT(30); // disable

  printf("preparing for an OUT transfer of %d packets / %d bytes\n", packets, bytes);

  udelay(2);

  // 9bit max for packet count
  ep1->size = (packets << 19) // packet count
    | (bytes) // total bytes
    ;
  dump_endpoint(ep1, false);
  uint32_t c = ep1->control;
  c |= BIT(31); // enable
  c |= BIT(26); // clear NAK
  c = (c & ~(3 << 18)) | (2 << 18); // endpoint type = bulk
  c |= BIT(15); // active
  c = (c & ~0x7ff) | 512; // mps
  ep1->control = c;
  dump_endpoint(ep1, false);
}

static void dump_endpoint(endpoint_control *ep, bool in) {
  if (ep->control) {
    uint32_t ctl = ep->control;
    printf("     CTL: 0x%08x\n", ctl);
    if ((ctl >> 15) & 1) puts("       Active");
    if ((ctl >> 17) & 1) puts("       NAK'ing");
    if ((ctl >> 31) & 1) puts("       Enabled");
    uint32_t irq = ep->interrupt;
    printf("     INT: 0x%08x\n", irq);
    if (irq & 1)          puts("       XFERCOMPL");
    if (in) {
      if ((irq >> 4) & 1)   puts("       INTKNTXFEMP IN Token Received When TxFIFO is Empty");
    } else {
      if (irq & BIT(4)) puts("       OUT token recieved when EP disabled");
    }
    if ((irq >> 6) & 1)   puts("       INEPNAKEFF  IN Endpoint NAK Effective");
    if ((irq >> 13) & 1)  puts("       NAKINTRPT   NAK Interrupt");
    if (in) {
      if ((irq >> 3) & 1) puts("       TIMEOUT");
      if ((irq >> 7) & 1) puts("       TX FIFO empty");
    } else {
      if ((irq >> 3) & 1) puts("       SETUP");
    }
    uint32_t size = ep->size;
    printf("    TSIZ: 0x%08x\n", size);
    printf("      MC: %d\n", (size >> 29) & 3);
    printf("      PktCnt: %d\n", (size >> 19) & 0x3ff);
    printf("      XferSize: %d\n", size & 0x7ffff);
    printf("  TXFSTS: 0x%08x\n", ep->fifo_status);
  }
}

APP_START(dwc2_host)
  .init = dwc2d_init,
APP_END
