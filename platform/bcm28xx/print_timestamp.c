#include <arch/arch_ops.h>
#include <lk/reg.h>
#include <platform/bcm28xx.h>
#include <platform/bcm28xx/clock.h>
#include <platform/bcm28xx/print_timestamp.h>
#include <stdio.h>

void print_timestamp() {
  uint32_t clock_lo = *REG32(ST_CLO);

#if 1
  clock_lo -= arch_init_timestamp;
#endif

  printf("%3d.%06d ", clock_lo / 1000000, clock_lo % 1000000);
}
