#pragma once

#include <platform/bcm28xx.h>

#define MHZ_TO_HZ(f) ((f)*1000*1000)

enum pll {
  PLL_A,
  PLL_B,
  PLL_C,
  PLL_D,
  PLL_H,

  PLL_NUM,
};

struct pll_def {
  char name[8];
  volatile uint32_t *ana;
  volatile uint32_t *dig;
  uint32_t enable_bit; // the bit to enable it within A2W_XOSC_CTRL
  volatile uint32_t *frac;
  volatile uint32_t *ctrl;
  uint32_t ndiv_mask;
  unsigned short ana1_prescale_bit;
  unsigned short cm_flock_bit;
  volatile uint32_t *cm_pll;
  volatile uint32_t *ana_kaip;
  volatile uint32_t *ana_vco;
};

extern const struct pll_def pll_def[PLL_NUM];

enum pll_chan {
  PLL_CHAN_ACORE,
  PLL_CHAN_APER,
  PLL_CHAN_ADSI0,
  PLL_CHAN_ACCP2,

  PLL_CHAN_BARM,
  PLL_CHAN_BSP0,
  PLL_CHAN_BSP1,
  PLL_CHAN_BSP2,

  PLL_CHAN_CCORE0,
  PLL_CHAN_CCORE1,
  PLL_CHAN_CCORE2,
  PLL_CHAN_CPER,

  PLL_CHAN_DCORE,
  PLL_CHAN_DPER,
  PLL_CHAN_DDSI0,
  PLL_CHAN_DDSI1,

  PLL_CHAN_HPIX,
  PLL_CHAN_HRCAL,
  PLL_CHAN_HAUX,

  PLL_CHAN_NUM,
};

struct pll_chan_def {
  char name[12];
  volatile uint32_t *ctrl;
  int chenb_bit;
  uint32_t div_mask;
  enum pll pll;
};

static const uint32_t xtal_freq = CRYSTAL;
extern unsigned int freq_pllc_core0;
extern uint64_t freq_pllc_per;
extern const struct pll_chan_def pll_chan_def[PLL_CHAN_NUM];

void setup_pllc(uint64_t freq, int core0_div, int per_div);
void setup_pllh(uint64_t freq);
void switch_vpu_to_src(int src);
bool clock_set_pwm(int freq, int source);
