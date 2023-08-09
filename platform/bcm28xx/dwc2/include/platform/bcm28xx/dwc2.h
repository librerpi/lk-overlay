#pragma once

#include <platform/bcm28xx.h>

#define USB_BASE (BCM_PERIPH_BASE_VIRT + 0x980000)

#define USB_GOTGCTL   (USB_BASE + 0x0000)
#define USB_GOTGINT   (USB_BASE + 0x0004)
#define USB_GAHBCFG   (USB_BASE + 0x0008)
#define USB_GUSBCFG   (USB_BASE + 0x000c)

#define USB_GRSTCTL   (USB_BASE + 0x0010)
#define USB_GINTSTS   (USB_BASE + 0x0014)
#define USB_GINTSTS_RXFLVL  (1 << 4)
#define USB_GINTSTS_SOF     (1 << 3)
#define USB_GINTMSK   (USB_BASE + 0x0018)
#define USB_GINTMSK_PortInt  BIT(24)
#define USB_GRXSTSR   (USB_BASE + 0x001c)

#define USB_GRXSTSP   (USB_BASE + 0x0020)
#define USB_GRXFSIZ   (USB_BASE + 0x0024)
#define USB_GNPTXFSIZ (USB_BASE + 0x0028)
#define USB_GNPTXSTS  (USB_BASE + 0x002c)

#define USB_GPVNDCTL  (USB_BASE + 0x0034)
#define USB_GUID      (USB_BASE + 0x003c)

#define USB_GHWCFG1   (USB_BASE + 0x0044)
#define USB_GHWCFG2   (USB_BASE + 0x0048)
#define USB_GHWCFG3   (USB_BASE + 0x004c)

#define USB_GHWCFG4   (USB_BASE + 0x0050)

#define USB_GMDIOCSR  (USB_BASE + 0x0080)
#define USB_GMDIOGEN  (USB_BASE + 0x0084)
#define USB_GVBUSDRV  (USB_BASE + 0x0088)

#define USB_HPTXFSIZ  (USB_BASE + 0x0100)
#define USB_DIEPTXF1  (USB_BASE + 0x0104)

#define USB_HCFG      (USB_BASE + 0x0400)
#define USB_HFIR      (USB_BASE + 0x0404)
#define USB_HFNUM     (USB_BASE + 0x0408)

#define USB_HPTXSTS   (USB_BASE + 0x0410)
#define USB_HAINT     (USB_BASE + 0x0414)
#define USB_HAINTMSK  (USB_BASE + 0x0418)

#define USB_HPRT      (USB_BASE + 0x0440)

#define USB_DCFG      (USB_BASE + 0x0800)
#define USB_DCTL      (USB_BASE + 0x0804)
#define USB_DSTS      (USB_BASE + 0x0808)
#define USB_DIEPMSK   (USB_BASE + 0x0810)
#define USB_DOEPMSK   (USB_BASE + 0x0814)
#define USB_DAINT     (USB_BASE + 0x0818)
#define USB_DAINTMSK  (USB_BASE + 0x081c)

// enable an endpoint, allowing packets to flow to/from the fifo
#define EP_ENABLE     BIT(31)
#define EP_SET_NAK    BIT(27)
#define EP_CLEAR_NAK  BIT(26)
// 0=control, 1=isochro, 2=bulk, 3=interrupt
#define EP_TYPE(n)    ((n & 3) << 18)
// activate an endpoint, inactive endpoints cause an instant failure rather then delay/stall/nak
#define EP_ACTIVE     BIT(15)
// EP0 has a 2bit enum, all others have a full 10bit int
#define EP_MAX_PACKET(n) (n & 0x3ff)

#define USB_DIEPCTL0  (USB_BASE + 0x0900)
#define USB_DIEPINT0  (USB_BASE + 0x0908)

#define USB_DIEPCTL1  (USB_BASE + 0x0920)
#define USB_DIEPINT1  (USB_BASE + 0x0928)

#define USB_DIEPCTL2  (USB_BASE + 0x0940)

#define USB_DOEPCTL0  (USB_BASE + 0x0b00)
#define USB_DOEPINT0  (USB_BASE + 0x0b08)

#define USB_DOEPCTL1  (USB_BASE + 0x0b20)
#define USB_DOEPINT1  (USB_BASE + 0x0b28)

#define USB_DOEPCTL2  (USB_BASE + 0x0b40)
#define USB_DOEPINT2  (USB_BASE + 0x0b48)

#define USB_DOEPCTL3  (USB_BASE + 0x0b60)
#define USB_DOEPINT3  (USB_BASE + 0x0b68)

#define USB_DOEPCTL4  (USB_BASE + 0x0b80)
#define USB_DOEPINT4  (USB_BASE + 0x0b88)
#define USB_DOEPINT5  (USB_BASE + 0x0ba8)
#define USB_DOEPINT6  (USB_BASE + 0x0bc8)
#define USB_DOEPINT7  (USB_BASE + 0x0be8)
#define USB_DOEPINT8  (USB_BASE + 0x0c08)
#define USB_DOEPINT9  (USB_BASE + 0x0c28)

#define USB_PCGCCTL   (USB_BASE + 0x0e00)

#define USB_DFIFO0    (USB_BASE + 0x1000)
#define USB_DFIFO1    (USB_BASE + 0x2000)
#define USB_DFIFO(n)  (USB_BASE + 0x1000 + (n*0x1000))

#define DWC_IRQ       9


#define HCCHARn_MAX_PACKET_SIZE(n) (n & 0x7ff)
#define HCCHARn_ENDPOINT(n) ((n & 0xf) << 11)
#define HCCHARn_OUT 0
#define HCCHARn_IN BIT(15)
#define HCCHARn_ADDR(n) ((n & 0x7f) << 22)
#define HCCHARn_ENABLE (BIT(31))

#define HCTSIZn_BYTES(n) (n & 0x7ffff)
#define HCTSIZn_PACKET_COUNT(n) ((n & 0x3ff) << 19)
#define HCTSIZn_PID(n) ((n & 3) << 29)
