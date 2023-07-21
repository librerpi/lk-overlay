#include <platform/bcm28xx/clock.h>
#include <lk/reg.h>

void board_init(void);

static inline uint32_t board_millis(void) {
  return *REG32(ST_CLO);
}

void board_led_write(int led) {
}
