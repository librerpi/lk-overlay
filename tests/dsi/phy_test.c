#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <lk/reg.h>
#include <platform/bcm28xx/dsi.h>
#include <platform/bcm28xx/pll.h>
#include <platform/bcm28xx/pll_read.h>
#include <platform/bcm28xx/udelay.h>

static uint32_t registers[1024], dsi_regs[64];
static unsigned calls, pll_fail, latch, delay_1ms, stop_running_clock, clock_accesses;
struct event { uint32_t reg, value; };
static struct event events[64];
uint32_t *test_reg(uint32_t address) {
  assert(address < 1024);
  /* Simulate hardware clearing read-only BUSY after the disable write. */
  if (address == 0x258 && stop_running_clock && ++clock_accesses == 3)
    registers[address] &= ~0x80u;
  return &registers[address];
}
int setup_plld(uint32_t freq, int divider) {
  assert(freq == 2000000000u && divider == 8);
  return pll_fail ? -1 : 0;
}
uint32_t get_pll_freq(enum pll pll) { assert(pll == PLL_D); return 2000000000u; }
uint32_t get_pll_chan_freq(enum pll_chan chan) { assert(chan == PLL_CHAN_DPER); return 250000000u; }
void udelay(uint32_t usec) { if (usec == 1000) ++delay_1ms; }
uint32_t bcm_dsi1_read(uint32_t offset) {
  assert(offset / 4 < 64);
  if (offset == BCM_DSI1_PHY_AFEC0 && latch) return dsi_regs[offset / 4] | (1u << 14);
  if (offset == BCM_DSI1_STAT && latch) return (1u << 24) | (1u << 25);
  return dsi_regs[offset / 4];
}
void bcm_dsi1_write(uint32_t offset, uint32_t value) {
  assert(calls < 64 && offset / 4 < 64);
  events[calls++] = (struct event){offset, value};
  dsi_regs[offset / 4] = value;
}
static void reset(void) {
  memset(registers, 0, sizeof(registers)); memset(dsi_regs, 0, sizeof(dsi_regs));
  calls = pll_fail = latch = delay_1ms = stop_running_clock = clock_accesses = 0;
}
int main(void) {
  struct bcm_dsi_clock_config cfg = { .pixel_hz_requested = 25979400u };
  reset();
  assert(bcm_dsi1_clock_init(NULL) == -1);
  assert(bcm_dsi1_clock_init(&cfg) == 0);
  assert(cfg.phy_hz == 666666666u && cfg.pixel_hz_actual == 27777777u && cfg.escape_hz == 100000000u);
  assert(registers[0x10] == (0x5a000000u | 3u));
  assert(registers[0x30] == 0x5a000005u);
  assert(registers[0x25c] == (0x5a000000u | 10240u));
  assert(bcm_dsi1_phy_init(&cfg) == 0);
  assert(events[0].reg == BCM_DSI1_INT_EN && events[1].reg == BCM_DSI1_CTRL);
  assert(dsi_regs[BCM_DSI1_DISP1_CTRL / 4] == 5);
  assert((dsi_regs[BCM_DSI1_PHY_AFEC0 / 4] & (1u << 13)) == 0);
  assert(registers[0x260] == (0x5a000000u | 0x58u));
  assert(bcm_dsi1_video_prepare() == 0 && bcm_dsi1_video_enable() == 0);
  assert(dsi_regs[BCM_DSI1_DISP0_CTRL / 4] == ((38u << 13) | (3u << 2) | (2u << 11) | (1u << 4) | 1u));
  assert(delay_1ms == 2);
  reset(); pll_fail = 1; assert(bcm_dsi1_clock_init(&cfg) == -1);
  reset(); registers[0x258] = 0x80; assert(bcm_dsi1_clock_init(&cfg) == -1);
  reset(); registers[0x258] = 0x90; stop_running_clock = 1;
  assert(bcm_dsi1_clock_init(&cfg) == 0);
  reset(); cfg.pixel_hz_requested = UINT32_MAX; assert(bcm_dsi1_clock_init(&cfg) == -1);
  cfg.pixel_hz_requested = 25979400u;
  reset(); latch = 1; assert(bcm_dsi1_phy_init(&cfg) == 0);
  assert(dsi_regs[BCM_DSI1_PHYC / 4] == ((1u << 0) | (1u << 16) | (1u << 18) | (6u << 20)));
  puts("DSI clock and PHY tests passed");
  return 0;
}
