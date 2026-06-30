#include <app.h>
#include <kernel/mutex.h>
#include <lib/ddcv2.h>
#include <lib/video_timing.h>
#include <lk/init.h>
#include <lk/reg.h>
#include <platform/bcm28xx/cm.h>
#include <platform/bcm28xx/hvs.h>
#include <platform/bcm28xx/pll.h>
#include <platform/bcm28xx/power.h>
#include <platform/bcm28xx/print_timestamp.h>
#include <platform/bcm28xx/pv.h>
#include <platform/bcm28xx/udelay.h>
#include <stdio.h>

#define logf(fmt, ...) { print_timestamp(); printf("[hdmi:%s:%d]: "fmt, __FUNCTION__, __LINE__, ##__VA_ARGS__); }

// based heavily on https://github.com/raspberrypi/linux/blob/rpi-5.15.y/drivers/gpu/drm/vc4/vc4_hdmi.c

#define BIT(n) (1<<n)

#define HD_HDM_CTL                  (HD_BASE + 0xc)
#define HD_HDM_CTL_ENABLE           BIT(0)
#define HD_HDM_CTL_ENDIAN           BIT(1)
#define HD_HDM_CTL_SW_RST           BIT(2)
#define HD_MAI_CTL                  (HD_BASE + 0x14)
#define HD_MAI_THR                  (HD_BASE + 0x18)
#define HD_MAI_FMT                  (HD_BASE + 0x1c)
#define HD_MAI_DAT                  (HD_BASE + 0x20)
#define HD_VID_CTL                  (HD_BASE + 0x38)
#define HD_CSC_CTL                  (HD_BASE + 0x40)
#define HD_CSC_CTL_ORDER(order)     ((order & 7) << 5)
#define HD_CSC_CTL_ORDER_RGB 0
#define HD_CSC_CTL_ORDER_BGR 1
#define HD_CSC_CTL_ORDER_BRG 2
#define HD_CSC_CTL_ORDER_GRB 3
#define HD_CSC_CTL_ORDER_GBR 4
#define HD_CSC_CTL_ORDER_RBG 5
#define HD_CSC_12_11                (HD_BASE + 0x44)
#define HD_CSC_14_13                (HD_BASE + 0x48)
#define HD_CSC_22_21                (HD_BASE + 0x4c)
#define HD_CSC_24_23                (HD_BASE + 0x50)
#define HD_CSC_32_31                (HD_BASE + 0x54)
#define HD_CSC_34_33                (HD_BASE + 0x58)
// frame counter reset
#define HD_VID_CTL_RST_FRAMEC       BIT(29)
// underflow enable
#define HD_VID_CTL_UFEN             BIT(30)
#define HD_VID_CTL_ENABLE           BIT(31)

#define HDMI_SW_RESET_CNTRL         (HDMI_BASE + 0x04)
#define HDMI_FIFO_CTL               (HDMI_BASE + 0x5c)
#define HDMI_FIFO_CTL_MASTER_SLAVE_N BIT(0)
#define HDMI_FIFO_CTL_RECENTER       BIT(6)
#define HDMI_FIFO_CTL_RECENTER_DONE  BIT(14)
#define HDMI_FIFO_VALID_WRITE_MASK   0xefff

#define HDMI_RAM_PACKET_CONFIG      (HDMI_BASE + 0xa0)
#define HDMI_SCHEDULER_CONTROL      (HDMI_BASE + 0xc0)
#define HDMI_SCHEDULER_CONTROL_IGNORE_VSYNC_PREDICTS  BIT(5)
#define HDMI_SCHEDULER_CONTROL_MANUAL_FORMAT BIT(15)
#define HDMI_VERTA0                 (HDMI_BASE + 0xcc)

#define HDMI_VERTB0                 (HDMI_BASE + 0xd0)
#define HDMI_VERTA1                 (HDMI_BASE + 0xd4)
#define HDMI_VERTB1                 (HDMI_BASE + 0xd8)

#define HDMI_HORZA                  (HDMI_BASE + 0xc4)
#define HDMI_HORZB                  (HDMI_BASE + 0xc8)


#define HDMI_MISC_CONTROL           (HDMI_BASE + 0xe4)

// TODO, merge
#define HDMI_TX_PHY_RESET_CTL        (HDMI_PHY_BASE + 0xc0)
#define HDMI_TX_PHY_TX_PHY_RESET_CTL (HDMI_PHY_BASE + 0xc0)

#define HDMI_SW_RESET_HDMI  BIT(0)
#define HDMI_SW_RESET_FORMAT_DETECT  BIT(1)

#define HDMI_SCHEDULER_CONTROL_IGN_VSYNC_PREDS_SET         0x00000020
#define HDMI_SCHEDULER_CONTROL_MANUAL_FORMAT_SET           0x00008000

#define HDMI_HORZA_HPOS BIT(13)
#define HDMI_HORZA_VPOS BIT(14)

// The HDMI encoder pixel pipeline latency. Its active window must start this
// many pixels after the pixelvalve's active start or the first pixels of each
// line show pipeline-fill garbage. See hdmi_set_timings().
#define HDMI_PIPELINE_DELAY 2

#define HDMI_MISC_CONTROL_PIXEL_REP_MASK  (3 << 4)

#define HD_VID_CTL_CLRRGB                   BIT(23)
#define HDMI_SCHEDULER_CONTROL_MODE_HDMI    BIT(0)
#define HDMI_SCHEDULER_CONTROL_HDMI_ACTIVE  BIT(1)
#define HDMI_RAM_PACKET_ENABLE              BIT(16)
#define HD_VID_CTL_BLANK_INSERT_EN          BIT(18)
#define HD_VID_CTL_BLANKPIX                 BIT(16)

#define A2W_HDMI_CTL0 0x7e102080
#define A2W_HDMI_CTL1 0x7e102084
#define A2W_HDMI_CTL2 0x7e102088
#define A2W_HDMI_CTL3 0x7e10208c

static void hdmi_set_timings(const struct pv_timings *t);
static void hdmi_phy_init(void);
static void hdmi_reset(void);
static void hdmi_recenter_fifo(void);

static struct pv_timings prefered_timings;

static void hdmi_init(uint level) {
  const struct pv_timings *active_timing;

  power_up_usb();
  hdmi_enable_power_domain();

  if (probe_ddcv2(&prefered_timings)) {
    active_timing = &prefered_timings;
  } else {
    active_timing = &pv_720p60;
  }

  print_timing_debug(active_timing);

  uint64_t pixel_clock = get_pixel_clock(active_timing);

  // Bring up PLLH so the HDMI pixel clock (PLLH_PIX) is live BEFORE we touch the
  // core. Stock firmware always programs the pixel clock during HDMI bring-up
  // (hdmi_open -> hdmi_set_pixel_clock), and platform.c only does this on the
  // 19.2MHz xtal path, so do it here to make HDMI bring-up self-contained.
  // VCO = pixel_clock*10; aux_div=5 -> PLLH_AUX = pixel_clock*2; HSM = /2 = pixel_clock.

  // TODO auto-select pix_div
  int pix_div = 2;
  setup_pllh(pixel_clock * 10 * pix_div, 8, pix_div);

  if (!clock_set_hsm(pixel_clock, PERI_PLLH_AUX)) {
    logf("HSM clock did not start, aborting HDMI bringup\n");
    return;
  }

  hdmi_reset();

  *REG32(HDMI_SCHEDULER_CONTROL) |= HDMI_SCHEDULER_CONTROL_MANUAL_FORMAT_SET | HDMI_SCHEDULER_CONTROL_IGN_VSYNC_PREDS_SET;

  hdmi_phy_init(); // handles TX PHY reset + config + bit24 commit pulse

  *REG32(A2W_HDMI_CTL3) = PM_PASSWORD | 0x40;
  *REG32(A2W_HDMI_CTL2) = PM_PASSWORD | 0x180086;
  *REG32(A2W_HDMI_CTL1) = PM_PASSWORD | 0x10c00;
  *REG32(A2W_HDMI_CTL0) = PM_PASSWORD | 0x430218;

  // PV2 pixel clock: source CM_DPICTL from PLLH_AUX (src=7, 324MHz) / 3 = 108MHz.
  // PLLH_AUX = 648MHz / aux_div=2 = 324MHz; 324/3 = 108MHz pixel clock.
  // CM_DPICTL is NOT used for HDMI on BCM2835, PV2 CLK_SELECT=DPI_SMI_HDMI
  // routes to HSM, not CM_DPICTL. Confirmed: working firmware has CM_DPICTL=0.
  setup_pixelvalve(active_timing, clk_dpi_smi_hdmi,2);

  hvs_initialize();

  mutex_acquire(&channels[1].lock);
  hvs_configure_channel(1, active_timing->hactive, active_timing->vactive, false);
  hvs_set_background_color(1, 0x00000000); // black background
  hvs_update_dlist(1);
  mutex_release(&channels[1].lock);

  *REG32(HDMI_SCHEDULER_CONTROL) = *REG32(HDMI_SCHEDULER_CONTROL) | HDMI_SCHEDULER_CONTROL_MANUAL_FORMAT | HDMI_SCHEDULER_CONTROL_IGNORE_VSYNC_PREDICTS;

  hdmi_set_timings(active_timing);

  printf("HDMI_FIFO_CTL: 0x%x\n", *REG32(HDMI_FIFO_CTL));
  *REG32(HDMI_FIFO_CTL) = HDMI_FIFO_CTL_MASTER_SLAVE_N;
  printf("HDMI_FIFO_CTL: 0x%x\n", *REG32(HDMI_FIFO_CTL));

  // RGB->YCC CSC matching working firmware (HD_CSC_CTL=0x2f)
  *REG32(HD_CSC_12_11) = (0x000 << 16) | 0x000;
  *REG32(HD_CSC_14_13) = (0x100 << 16) | 0x6e0;
  *REG32(HD_CSC_22_21) = (0x6e0 << 16) | 0x000;
  *REG32(HD_CSC_24_23) = (0x100 << 16) | 0x000;
  *REG32(HD_CSC_32_31) = (0x000 << 16) | 0x6e0;
  *REG32(HD_CSC_34_33) = (0x100 << 16) | 0x000;
  *REG32(HD_CSC_CTL)   = HD_CSC_CTL_ORDER(HD_CSC_CTL_ORDER_BGR);

  // Match working BCM2835 Raspbian ramdump exactly: ENABLE(bit31) | UNDERFLOW_ENABLE(bit30) = 0xc0000000.
  *REG32(HD_VID_CTL) = HD_VID_CTL_ENABLE | HD_VID_CTL_UFEN;
  // HDMI mode: set MODE_HDMI and wait for HDMI_ACTIVE to go high.
  *REG32(HDMI_RAM_PACKET_CONFIG) &= ~HDMI_RAM_PACKET_ENABLE;
  *REG32(HDMI_SCHEDULER_CONTROL) |= HDMI_SCHEDULER_CONTROL_MODE_HDMI;
  while (!(*REG32(HDMI_SCHEDULER_CONTROL) & HDMI_SCHEDULER_CONTROL_HDMI_ACTIVE)) {};
  *REG32(HDMI_RAM_PACKET_CONFIG) = HDMI_RAM_PACKET_ENABLE;

  hdmi_recenter_fifo();
}

static void hdmi_recenter_fifo(void) {
  uint32_t drift = *REG32(HDMI_FIFO_CTL);
  drift &= HDMI_FIFO_VALID_WRITE_MASK;
  *REG32(HDMI_FIFO_CTL) = drift & ~HDMI_FIFO_CTL_RECENTER;
  *REG32(HDMI_FIFO_CTL) = drift | HDMI_FIFO_CTL_RECENTER;

  udelay(1000);
  *REG32(HDMI_FIFO_CTL) = drift & ~HDMI_FIFO_CTL_RECENTER;
  *REG32(HDMI_FIFO_CTL) = drift | HDMI_FIFO_CTL_RECENTER;

  // Kernel waits until RECENTER_DONE asserts after the second pulse. Skipping
  // this leaves the HDMI video FIFO settled up to one pixel off.
  for (int i = 0; i < 100000; i++) {
    if (*REG32(HDMI_FIFO_CTL) & HDMI_FIFO_CTL_RECENTER_DONE) break;
    udelay(1);
  }
}

static void hdmi_reset(void) {
  // a small delay between each control is needed or it stalls somewhere.
  // tested by logging where the stall is only to find the stall goes poof with logs.
  // so we delay instead.
  logf("\n");
  *REG32(HD_HDM_CTL) = HD_HDM_CTL_SW_RST;
  udelay(10000);
  *REG32(HD_HDM_CTL) = 0;
  udelay(100);
  *REG32(HD_HDM_CTL) = HD_HDM_CTL_ENABLE;
  udelay(100);
  *REG32(HDMI_SW_RESET_CNTRL) = HDMI_SW_RESET_HDMI | HDMI_SW_RESET_FORMAT_DETECT;
  udelay(100);
  *REG32(HDMI_SW_RESET_CNTRL) = 0;
}

static void hdmi_set_timings(const struct pv_timings *t) {
  logf("\n");

  int pixel_rep = 1;

  // Horizontal timing
  int hactive = t->hactive;
  int hfp = t->hfp;
  int hsync = t->hsync;
  int hbp = t->hbp;

  uint32_t horza = HDMI_HORZA_VPOS | HDMI_HORZA_HPOS | hactive;

  // The HDMI encoder's video pipeline takes ~2 pixels to prime, so if its active
  // window starts exactly at the PV's active start, the first 1-2 pixels of each
  // line are pipeline-fill garbage (a faint strip down the left edge). Shift the
  // encoder window right by HDMI_PIPELINE_DELAY (HBP+, HFP-, line total
  // unchanged) so it samples past the garbage. Verified on BCM2835 480p60, 720p60, and 1920p60
  uint32_t horzb = ((hbp + HDMI_PIPELINE_DELAY) << 20) | (hsync << 10) | (hfp - HDMI_PIPELINE_DELAY);

  // Vertical timing
  int vactive = t->vactive;
  int vfp = t->vfp;
  int vsync = t->vsync;
  int vbp = t->vbp;

  uint32_t verta = (vsync << 20) | (vfp << 13) | (vactive);

  uint32_t vertb = (vbp << 0);

  uint32_t vertb0 = vertb; // even frame (no offset)

  *REG32(HDMI_HORZA) = horza;
  *REG32(HDMI_HORZB) = horzb;

  *REG32(HDMI_VERTA0) = verta;
  *REG32(HDMI_VERTA1) = verta;

  *REG32(HDMI_VERTB0) = vertb0;
  *REG32(HDMI_VERTB1) = vertb;

  // pixel replication control
  uint32_t misc = *REG32(HDMI_MISC_CONTROL);
  misc &= ~HDMI_MISC_CONTROL_PIXEL_REP_MASK;
  misc |= (pixel_rep - 1) << 4;
  *REG32(HDMI_MISC_CONTROL) = misc;
}

static void hdmi_phy_init(void) {
  logf("\n");

  // TX PHY register values from working BCM2835 firmware dump (1920x1080@60).
  // Linux relies on start.elf to configure these; we must do it explicitly.
  *REG32(HDMI_PHY_BASE + 0xc4) = 0x8e000000;
  *REG32(HDMI_PHY_BASE + 0xc8) = 0x0404a808;
  *REG32(HDMI_PHY_BASE + 0xcc) = 0x00a63004;
  *REG32(HDMI_PHY_BASE + 0xd0) = 0x2ff80112;
  *REG32(HDMI_PHY_BASE + 0xd4) = 0x0000001f;
  *REG32(HDMI_PHY_BASE + 0xd8) = 0x0000000f;
  *REG32(HDMI_PHY_BASE + 0xdc) = 0x00003c00;
  *REG32(HDMI_PHY_BASE + 0xe0) = 0xffff0000;

  // Assert TX PHY reset, then perform the bit24 "commit" pulse on HDMI_PHY_BASE + 0xc4
  // matching the sequence in start.elf hdmi_open after hdmi_set_pixel_clock.
  // The firmware clears bit25 (briefly gates the PHY), pulses bit24 (commits
  // config to analog), then deasserts reset.
  *REG32(HDMI_TX_PHY_RESET_CTL) = 0xf << 16;
  uint32_t r = *REG32(HDMI_PHY_BASE + 0xc4);
  r &= ~BIT(25);
  *REG32(HDMI_PHY_BASE + 0xc4) = r;
  r = *REG32(HDMI_PHY_BASE + 0xc4);
  r |= BIT(24);
  *REG32(HDMI_PHY_BASE + 0xc4) = r;
  r = *REG32(HDMI_PHY_BASE + 0xc4);
  r &= ~BIT(24);
  *REG32(HDMI_PHY_BASE + 0xc4) = r;
  // Restore bit25 and deassert reset. Working BCM2835 ramdump shows 0x8e000000 (bit25=1).
  r = *REG32(HDMI_PHY_BASE + 0xc4);
  r |= BIT(25);
  *REG32(HDMI_PHY_BASE + 0xc4) = r;
  *REG32(HDMI_TX_PHY_RESET_CTL) = 0;
  logf("TX PHY c4=0x%x c8=0x%x cc=0x%x e0=0x%x\n", *REG32(HDMI_PHY_BASE + 0xc4), *REG32(HDMI_PHY_BASE + 0xc8), *REG32(HDMI_PHY_BASE + 0xcc), *REG32(HDMI_PHY_BASE + 0xe0));
}

LK_INIT_HOOK(hdmi, &hdmi_init, LK_INIT_LEVEL_PLATFORM+10);
