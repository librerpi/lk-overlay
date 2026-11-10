#pragma once
#include <platform/bcm28xx/pll.h>
uint32_t get_pll_freq(enum pll pll);
uint32_t get_pll_chan_freq(enum pll_chan chan);
