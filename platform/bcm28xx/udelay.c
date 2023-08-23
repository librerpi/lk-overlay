#include <lk/reg.h>
#include <platform/bcm28xx.h>
#include <platform/bcm28xx/clock.h>
#include <platform/bcm28xx/udelay.h>
#include <stdint.h>

void udelay(uint32_t t) {
  uint32_t tv = *REG32(ST_CLO);
  uint32_t goal = tv + t;
  for (;;) {
    if ((*REG32(ST_CLO)) > goal) return;
  }
}
