#include <lk/reg.h>
#include <platform/bcm28xx/a2w.h>
#include <platform/bcm28xx/cm.h>
#include <platform/bcm28xx/dsi.h>
#include <platform/bcm28xx/pll.h>
#include <platform/bcm28xx/pll_read.h>
#include <platform/bcm28xx/power.h>
#include <platform/bcm28xx/udelay.h>
#include <stdio.h>

#define DSI_CM_ENABLE (1u << 4)
#define DSI_CM_GATE (1u << 6)
#define DSI_CM_BUSY (1u << 7)
#define DSI_CM_FRAC (1u << 9)
#define DSI_CM_DSI1E_CTL (CM_BASE + 0x158)
#define DSI_CM_DSI1E_DIV (CM_BASE + 0x15c)
#define DSI_CM_DSI1P_CTL (CM_BASE + 0x160)
#define DSI_CM_DSI1P_DIV (CM_BASE + 0x164)
#define PLLD_HOLD_DSI1 (1u << 3)
#define PLLD_LOAD_DSI1 (1u << 2)
#define PLLD_HOLD_PER (1u << 7)
#define PLLD_LOAD_PER (1u << 6)

#define DSI_CTRL_RESET_FIFOS ((1u << 7) | (1u << 6) | (1u << 5) | (1u << 4))
#define DSI_INT_ERROR_MASK ((1u << 22) | (0xfu << 6) | (0x3u << 8) | (0xfu << 10))
#define DSI_AFEC_RESET (1u << 13)
#define DSI_AFEC_LATCH_ULPS (1u << 14)
#define DSI_PHY_D0_ULPS (1u << 1)
#define DSI_STAT_D0_STOP (1u << 24)
#define DSI_STAT_D0_ULPS (1u << 25)

static int wait_clock(uint32_t ctl) {
  unsigned int timeout = 100000;
  while (*REG32(ctl) & DSI_CM_BUSY) {
    if (!timeout--) return -1;
  }
  return 0;
}

static int set_clock(uint32_t ctl, uint32_t divreg, unsigned int source, uint32_t divider) {
  uint32_t value = *REG32(ctl);
  /* BUSY may be set while the clock is running. Disable it before waiting. */
  *REG32(ctl) = CM_PASSWORD | (value & ~DSI_CM_ENABLE);
  if (wait_clock(ctl)) return -1;
  *REG32(divreg) = CM_PASSWORD | divider;
  *REG32(ctl) = CM_PASSWORD | ((value & ~(0xfu | DSI_CM_FRAC)) | source |
                               ((divider & 0xfffu) ? DSI_CM_FRAC : 0) |
                               DSI_CM_ENABLE | DSI_CM_GATE);
  /* BUSY is expected to be set once the clock runs; do not wait for it to clear. */
  return 0;
}

/* Derived from the working vc4_dsi and clk-bcm2835 initialization sequence. */
int bcm_dsi1_clock_init(struct bcm_dsi_clock_config *cfg) {
  uint32_t cm, plld_hz, dper_hz, requested, phy_div, escape_div;
  if (!cfg || !cfg->pixel_hz_requested || cfg->pixel_hz_requested > UINT32_MAX / 24u) return -1;
  *REG32(PM_DSI1) = PM_PASSWORD | PM_DSI1_CTRLEN;
  udelay(1);
  *REG32(PM_DSI1) = PM_PASSWORD | PM_DSI1_CTRLEN | PM_DSI1_LDOHPEN;
  udelay(100);
  if (setup_plld(2000000000u, 8)) return -1;
  cm = *REG32(CM_PLLD);
  *REG32(CM_PLLD) = CM_PASSWORD | (cm | PLLD_LOAD_PER);
  *REG32(CM_PLLD) = CM_PASSWORD | (cm & ~(PLLD_LOAD_PER | PLLD_HOLD_PER));
  plld_hz = get_pll_freq(PLL_D);
  dper_hz = get_pll_chan_freq(PLL_CHAN_DPER);
  requested = cfg->pixel_hz_requested * 24u;
  if (!plld_hz || !dper_hz || !requested) {
    printf("DSI: invalid PLLD clocks (PLLD=%u DPER=%u requested=%u)\n",
           plld_hz, dper_hz, requested);
    return -1;
  }
  phy_div = plld_hz / requested;
  if (!phy_div) phy_div = 1;
  if (phy_div > 255) return -1;
  cm = *REG32(CM_PLLD);
  *REG32(CM_PLLD) = CM_PASSWORD | ((cm & ~PLLD_LOAD_DSI1) | PLLD_HOLD_DSI1);
  *REG32(A2W_PLLD_DSI1) = A2W_PASSWORD | phy_div;
  *REG32(CM_PLLD) = CM_PASSWORD | (cm | PLLD_LOAD_DSI1);
  *REG32(CM_PLLD) = CM_PASSWORD | (cm & ~(PLLD_LOAD_DSI1 | PLLD_HOLD_DSI1));
  escape_div = (((uint64_t)dper_hz << 12) + 100000000u - 1) / 100000000u;
  escape_div &= ~0xfu;
  if (!escape_div || set_clock(DSI_CM_DSI1E_CTL, DSI_CM_DSI1E_DIV, 6, escape_div)) {
    puts("DSI: escape clock did not stop");
    return -1;
  }
  cfg->phy_hz = plld_hz / phy_div;
  cfg->pixel_hz_actual = cfg->phy_hz / 24u;
  cfg->escape_hz = (uint32_t)(((uint64_t)dper_hz << 12) / escape_div);
  return 0;
}

static uint32_t hs_timing(uint32_t ui_ns, uint32_t ns, uint32_t ui) {
  return (ui + (ns + ui_ns - 1) / ui_ns + 7) & ~7u;
}
static uint32_t field(uint32_t value, unsigned int shift, uint32_t mask) {
  return (value & mask) << shift;
}
static int wait_status(uint32_t mask) {
  for (unsigned int timeout = 100000; timeout; --timeout) {
    if ((bcm_dsi1_read(BCM_DSI1_STAT) & mask) == mask) return 0;
    udelay(1);
  }
  return -1;
}

int bcm_dsi1_phy_init(const struct bcm_dsi_clock_config *cfg) {
  uint32_t ui_ns, afec0, phyc;
  const uint32_t lpx = 7;
  if (!cfg || !cfg->phy_hz || !cfg->escape_hz) return -1;
  bcm_dsi1_write(BCM_DSI1_INT_EN, DSI_INT_ERROR_MASK);
  ui_ns = (500000000u + cfg->phy_hz - 1) / cfg->phy_hz;
  if (!ui_ns) return -1;
  bcm_dsi1_write(BCM_DSI1_CTRL, (1u << 10) | DSI_CTRL_RESET_FIFOS);
  bcm_dsi1_write(BCM_DSI1_CTRL, (1u << 0) | (1u << 11) | (1u << 13));
  bcm_dsi1_write(BCM_DSI1_STAT, bcm_dsi1_read(BCM_DSI1_STAT));
  afec0 = field(7, 4, 0xf) | field(7, 0, 0xf) | field(6, 17, 7) |
          field(6, 20, 7) | field(6, 23, 7) | field(6, 26, 7) |
          field(6, 29, 7) | DSI_AFEC_RESET | (1u << 10) | (1u << 9) | (1u << 8);
  bcm_dsi1_write(BCM_DSI1_PHY_AFEC0, afec0);
  bcm_dsi1_write(BCM_DSI1_PHY_AFEC1, 0);
  udelay(1000);
  if (set_clock(DSI_CM_DSI1P_CTL, DSI_CM_DSI1P_DIV, 8, 0)) {
    puts("DSI: PHY clock did not stop");
    return -1;
  }
  bcm_dsi1_write(BCM_DSI1_HS_CLT0, field(hs_timing(ui_ns, 262, 0), 18, 0x1ff) |
                 field(hs_timing(ui_ns, 0, 8), 9, 0x1ff) | field(hs_timing(ui_ns, 38, 0), 0, 0x1ff));
  bcm_dsi1_write(BCM_DSI1_HS_CLT1, field(hs_timing(ui_ns, 60, 0), 9, 0x1ff) |
                 field(hs_timing(ui_ns, 60, 52), 0, 0x1ff));
  bcm_dsi1_write(BCM_DSI1_HS_CLT2, hs_timing(ui_ns, 1000000, 0));
  bcm_dsi1_write(BCM_DSI1_HS_DLT3, field(hs_timing(ui_ns, 100, 0), 18, 0x1ff) |
                 field(hs_timing(ui_ns, 105, 6), 9, 0x1ff) | field(hs_timing(ui_ns, 40, 4), 0, 0x1ff));
  bcm_dsi1_write(BCM_DSI1_HS_DLT4, field(hs_timing(ui_ns, 60, 52), 9, 0x1ff) |
                 field(hs_timing(ui_ns, lpx * 10, 0), 0, 0x1ff));
  bcm_dsi1_write(BCM_DSI1_HS_DLT5, hs_timing(ui_ns, 5000000, 0));
  bcm_dsi1_write(BCM_DSI1_HS_DLT6, field(lpx * 5, 24, 0xff) | field(lpx, 16, 0xff) |
                 field(lpx * 4, 8, 0xff) | field(lpx, 0, 0xff));
  bcm_dsi1_write(BCM_DSI1_HS_DLT7, (1000000u + 9) / 10);
  phyc = (1u << 0) | (1u << 16) | (1u << 18) | field(lpx - 1, 20, 0x3f);
  bcm_dsi1_write(BCM_DSI1_PHYC, phyc);
  bcm_dsi1_write(BCM_DSI1_CTRL, bcm_dsi1_read(BCM_DSI1_CTRL) | (1u << 9));
  bcm_dsi1_write(BCM_DSI1_HSTX_TO_CNT, 0);
  bcm_dsi1_write(BCM_DSI1_LPRX_TO_CNT, 0xffffff);
  bcm_dsi1_write(BCM_DSI1_TA_TO_CNT, 100000);
  bcm_dsi1_write(BCM_DSI1_PR_TO_CNT, 100000);
  bcm_dsi1_write(BCM_DSI1_DISP1_CTRL, (2u << 1) | 1u);
  bcm_dsi1_write(BCM_DSI1_PHY_AFEC0, afec0 & ~DSI_AFEC_RESET);
  if (bcm_dsi1_read(BCM_DSI1_PHY_AFEC0) & DSI_AFEC_LATCH_ULPS) {
    bcm_dsi1_write(BCM_DSI1_STAT, DSI_STAT_D0_ULPS);
    bcm_dsi1_write(BCM_DSI1_PHYC, phyc | DSI_PHY_D0_ULPS);
    if (wait_status(DSI_STAT_D0_ULPS)) return -1;
    bcm_dsi1_write(BCM_DSI1_PHY_AFEC0, afec0 & ~(DSI_AFEC_RESET | DSI_AFEC_LATCH_ULPS));
    bcm_dsi1_write(BCM_DSI1_STAT, DSI_STAT_D0_STOP);
    bcm_dsi1_write(BCM_DSI1_PHYC, phyc);
    if (wait_status(DSI_STAT_D0_STOP)) return -1;
  }
  return 0;
}

int bcm_dsi1_video_prepare(void) {
  bcm_dsi1_write(BCM_DSI1_DISP0_CTRL, (38u << 13) | (3u << 2) | (2u << 11) | (1u << 4));
  return 0;
}
int bcm_dsi1_video_enable(void) {
  bcm_dsi1_write(BCM_DSI1_DISP0_CTRL, (38u << 13) | (3u << 2) | (2u << 11) | (1u << 4) | 1u);
  udelay(1000);
  return 0;
}
