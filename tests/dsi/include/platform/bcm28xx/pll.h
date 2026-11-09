#pragma once
#include <stdint.h>
enum pll { PLL_D };
enum pll_chan { PLL_CHAN_DPER };
int setup_plld(uint32_t freq, int divider);
