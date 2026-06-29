#include <dev/gpio.h>
#include <lk/reg.h>
#include <platform/bcm28xx/board.h>
#include <platform/bcm28xx/cm.h>
#include <platform/bcm28xx/gpio.h>
#include <platform/bcm28xx/otp.h>
#include <platform/bcm28xx/pll_read.h>
#include <platform/bcm28xx/udelay.h>
#include <stdio.h>

static const board_pins_t pi2b = {
  .lan_run = 31,
  .ethclk = 44,
};

static const board_pins_t pi3b = {
  .lan_run = 29,
  .ethclk = 42,
};

static const board_pins_t pi3bplus = {
  .lan_run = 30, // labeled as LAN_RUN_BOOT
  .ethclk = 42,
  // expander pin 3(LAN_RUN) goes to pin 12 (nRESET) on the LAN9514
};

const board_pins_t *current_board;

void board_init(void) {
  uint32_t revision = otp_read(30);
  uint32_t type = (revision >> 4) & 0xff;
  printf("booting on board type: 0x%x\n", type);
  switch (type) {
  case 4: // 2B
    current_board = &pi2b;
    break;
  case 8: // 3B
    current_board = &pi3b;
    break;
  case 0xd: // 3B+
    current_board = &pi3bplus;
    break;
  default:
    break;
  }

  if (current_board->lan_run) {
    int lan_run = current_board->lan_run;
    gpio_config(lan_run, kBCM2708PinmuxOut);
    gpio_set(lan_run, 0);
    int ethclk_pin = current_board->ethclk;
    if (ethclk_pin > 0) {
      // GP1 routed to GPIO42 to drive ethernet/usb chip
      *REG32(CM_GP1CTL) = CM_PASSWORD | CM_GPnCTL_KILL_SET;
      while (*REG32(CM_GP1CTL) & CM_GPnCTL_BUSY_SET) {};

      uint32_t divisor = freq_pllc_per / (25000000/0x1000);

      *REG32(CM_GP1CTL) = CM_PASSWORD | (2 << CM_GPnCTL_MASH_LSB) | CM_SRC_PLLC_CORE0;
      *REG32(CM_GP1DIV) = CM_PASSWORD | divisor; // divisor * 0x1000
      *REG32(CM_GP1CTL) = CM_PASSWORD | (2 << CM_GPnCTL_MASH_LSB) | CM_SRC_PLLC_CORE0 | CM_GPnCTL_ENAB_SET;

      gpio_config(ethclk_pin, kBCM2708Pinmux_ALT0);
    }

    udelay(10*1000);

    gpio_set(lan_run, 1);
  }
}
