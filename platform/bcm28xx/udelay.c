#include <lk/reg.h>
#include <platform/bcm28xx.h>
#include <platform/bcm28xx/clock.h>
#include <platform/bcm28xx/udelay.h>
#include <stdint.h>

void udelay(uint32_t t) {
  uint32_t tv = *REG32(ST_CLO);
  for (;;) {
    /* nop still takes a cycle i think? */
    __asm__ __volatile__ ("nop" :::);
    if ((*REG32(ST_CLO) - tv) > t) return;
  }
}
