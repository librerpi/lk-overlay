#include <app.h>
#include <lk/init.h>
#include <lk/reg.h>
#include <platform/bcm28xx/cm.h>
#include <platform/bcm28xx/pll.h>
#include <platform/bcm28xx/power.h>
#include <platform/bcm28xx/udelay.h>
#include <stdio.h>
#include <platform/bcm28xx/print_timestamp.h>

#define logf(fmt, ...) { print_timestamp(); printf("[hdmi:%s:%d]: "fmt, __FUNCTION__, __LINE__, ##__VA_ARGS__); }

// based heavily on https://github.com/raspberrypi/linux/blob/rpi-5.15.y/drivers/gpu/drm/vc4/vc4_hdmi.c

#define BIT(n) (1<<n)

#define HD_HDM_CTL                  0x7e80800c
#define HD_HDM_CTL_ENABLE           BIT(0)
#define HD_HDM_CTL_ENDIAN           BIT(1)
#define HD_HDM_CTL_SW_RST           BIT(2)
#define HD_MAI_CTL                  0x7e808014
#define HD_MAI_THR                  0x7e808018
#define HD_MAI_FMT                  0x7e80801c
#define HD_MAI_DAT                  0x7e808020
#define HDMI_VID_CTL                0x7e808038
// frame counter reset
#define HD_VID_CTL_RST_FRAMEC       BIT(29)
// underflow enable
#define HD_VID_CTL_UFEN             BIT(30)
#define HD_VID_CTL_ENABLE           BIT(31)

#define HDMI_SW_RESET_CNTRL         0x7e902004
#define HDMI_FIFO_CTL               0x7e90205c
#define VC4_HDMI_FIFO_CTL_MASTER_SLAVE_N                  BIT(0)
#define VC4_HDMI_FIFO_CTL_RECENTER                        BIT(6)
#define VC4_HDMI_FIFO_CTL_RECENTER_DONE                   BIT(14)
#define VC4_HDMI_FIFO_VALID_WRITE_MASK                    0xefff

#define HDMI_RAM_PACKET_CONFIG      0x7e9020a0
#define HDMI_SCHEDULER_CONTROL      0x7e9020c0
#define VC4_HDMI_SCHEDULER_CONTROL_IGNORE_VSYNC_PREDICTS  BIT(5)
#define VC4_HDMI_SCHEDULER_CONTROL_MANUAL_FORMAT BIT(15)
#define HDMI_VERTA0           0x7e9020cc

#define HDMI_VERTB0           0x7e9020d0
#define HDMI_VERTA1           0x7e9020d4
#define HDMI_VERTB1           0x7e9020d8

#define HDMI_MISC_CONTROL     0x7e9020e4

// TODO, merge
#define HDMI_TX_PHY_RESET_CTL        0x7e9022c0
#define HDMI_TX_PHY_TX_PHY_RESET_CTL 0x7e9022c0

#define VC4_HDMI_SW_RESET_HDMI  BIT(0)
#define VC4_HDMI_SW_RESET_FORMAT_DETECT   BIT(1)

#define HDMI_SCHEDULER_CONTROL_IGN_VSYNC_PREDS_SET         0x00000020
#define HDMI_SCHEDULER_CONTROL_MANUAL_FORMAT_SET           0x00008000


#define VC4_HDMI_HORZA_HPOS BIT(13)
#define VC4_HDMI_HORZA_VPOS BIT(14)

#define VC4_HDMI_MISC_CONTROL_PIXEL_REP_MASK  (3 << 4)

#define VC4_HD_VID_CTL_CLRRGB BIT(23)
#define VC4_HDMI_SCHEDULER_CONTROL_MODE_HDMI    BIT(0)
#define VC4_HDMI_SCHEDULER_CONTROL_HDMI_ACTIVE BIT(1)
#define VC4_HDMI_RAM_PACKET_ENABLE              BIT(16)
#define VC4_HD_VID_CTL_BLANKPIX                 BIT(18)

static void vc4_hdmi_set_timings(void);
static void vc4_hdmi_phy_init(void);
static void vc4_hdmi_reset(void);

static void hdmi_init(uint level) {
  power_up_usb();
  //setup_pllh(1 * 1000 * 1000 * 1000);

  clock_set_hsm(MHZ_TO_HZ(125), PERI_PLLC_PER);
  vc4_hdmi_reset();

  *REG32(HDMI_TX_PHY_TX_PHY_RESET_CTL) = 0xf << 16;
  *REG32(HDMI_TX_PHY_TX_PHY_RESET_CTL) = 0;
  *REG32(HDMI_SCHEDULER_CONTROL) |= HDMI_SCHEDULER_CONTROL_MANUAL_FORMAT_SET | HDMI_SCHEDULER_CONTROL_IGN_VSYNC_PREDS_SET;

  vc4_hdmi_phy_init();

  *REG32(HDMI_SCHEDULER_CONTROL) = *REG32(HDMI_SCHEDULER_CONTROL) | VC4_HDMI_SCHEDULER_CONTROL_MANUAL_FORMAT | VC4_HDMI_SCHEDULER_CONTROL_IGNORE_VSYNC_PREDICTS;

  vc4_hdmi_set_timings();

  printf("HDMI_FIFO_CTL: 0x%x\n", *REG32(HDMI_FIFO_CTL));
  *REG32(HDMI_FIFO_CTL) = VC4_HDMI_FIFO_CTL_MASTER_SLAVE_N;
  printf("HDMI_FIFO_CTL: 0x%x\n", *REG32(HDMI_FIFO_CTL));

  *REG32(HDMI_VID_CTL) = HD_VID_CTL_ENABLE | VC4_HD_VID_CTL_CLRRGB | HD_VID_CTL_UFEN | HD_VID_CTL_RST_FRAMEC;

  *REG32(HDMI_VID_CTL) &= ~VC4_HD_VID_CTL_BLANKPIX;
  bool hdmi = false;
  if (hdmi) {
    // TODO
  } else {
    *REG32(HDMI_RAM_PACKET_CONFIG) &= ~VC4_HDMI_RAM_PACKET_ENABLE;
    *REG32(HDMI_SCHEDULER_CONTROL) &= ~VC4_HDMI_SCHEDULER_CONTROL_MODE_HDMI;
    while ((*REG32(HDMI_SCHEDULER_CONTROL) & VC4_HDMI_SCHEDULER_CONTROL_HDMI_ACTIVE) == 0) {};

    *REG32(HDMI_RAM_PACKET_CONFIG) = VC4_HDMI_RAM_PACKET_ENABLE;
  }

  uint32_t drift = *REG32(HDMI_FIFO_CTL);
  drift &= VC4_HDMI_FIFO_VALID_WRITE_MASK;
  *REG32(HDMI_FIFO_CTL) = drift & ~VC4_HDMI_FIFO_CTL_RECENTER;
  *REG32(HDMI_FIFO_CTL) = drift | VC4_HDMI_FIFO_CTL_RECENTER;

  udelay(1000);
  *REG32(HDMI_FIFO_CTL) = drift & ~VC4_HDMI_FIFO_CTL_RECENTER;
  *REG32(HDMI_FIFO_CTL) = drift | VC4_HDMI_FIFO_CTL_RECENTER;

  printf("HDMI_FIFO_CTL: 0x%x\n", *REG32(HDMI_FIFO_CTL));
  puts("waiting for recenter");
  //while (*REG32(HDMI_FIFO_CTL) & VC4_HDMI_FIFO_CTL_RECENTER_DONE) {}
  puts("done");
}

static void vc4_hdmi_reset(void) {
  logf("\n");
  *REG32(HD_HDM_CTL) = HD_HDM_CTL_SW_RST;
  udelay(1);
  *REG32(HD_HDM_CTL) = 0;
  *REG32(HD_HDM_CTL) = HD_HDM_CTL_ENABLE;

  *REG32(HDMI_SW_RESET_CNTRL) = VC4_HDMI_SW_RESET_HDMI | VC4_HDMI_SW_RESET_FORMAT_DETECT;
  *REG32(HDMI_SW_RESET_CNTRL) = 0;
}

static void vc4_hdmi_set_timings(void) {
  logf("\n");
  int pixel_rep = 1;
  int vsync = 3;
  int vfp = 1;
  int vactive = 1024;
  uint32_t verta = (vsync << 20) | (vfp << 13) | (vactive);
  printf("VERTA: 0x%x\n", verta);

  int vbp = 38;
  int vspo = 0;
  uint32_t vertb = (vspo << 9) | (vbp);
  printf("VERTB: 0x%x\n", vertb);

  int hactive = 1280;
  uint32_t horza = VC4_HDMI_HORZA_VPOS | VC4_HDMI_HORZA_HPOS | hactive;
  printf("HORZA: 0x%x\n", horza);

  int hbp = 248;
  int hsync = 112;
  int hfp = 48;
  uint32_t horzb = (hbp << 20) | (hsync << 10) | (hfp);
  printf("HORZB: 0x%x\n", horzb);

  int vsync_offset = 0;
  uint32_t vertb_even = (vsync_offset << 9) | (vbp);
  printf("VERTB0: 0x%x\n", vertb_even);

  *REG32(HDMI_VERTA0) = verta;
  *REG32(HDMI_VERTA1) = verta;

  *REG32(HDMI_VERTB0) = vertb_even;
  *REG32(HDMI_VERTB1) = vertb;

  uint32_t t = *REG32(HDMI_MISC_CONTROL);
  t &= ~VC4_HDMI_MISC_CONTROL_PIXEL_REP_MASK;
  t |= (pixel_rep-1) << 4;
  *REG32(HDMI_MISC_CONTROL) = t;
}

static void vc4_hdmi_phy_init(void) {
  logf("\n");
  *REG32(HDMI_TX_PHY_RESET_CTL) = 0xf << 16;
  *REG32(HDMI_TX_PHY_RESET_CTL) = 0;
}

LK_INIT_HOOK(hdmi, &hdmi_init, LK_INIT_LEVEL_PLATFORM+10);
