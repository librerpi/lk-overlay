#include <arch/ops.h>
#include <dev/gpio.h>
#include <lk/init.h>
#include <lk/reg.h>
#include <platform/bcm28xx.h>
#include <platform/bcm28xx/arm.h>
#include <platform/bcm28xx/arm_chainloader.h>
#include <platform/bcm28xx/cm.h>
#include <platform/bcm28xx/display.h>
#include <platform/bcm28xx/gpio.h>
#include <platform/bcm28xx/otp.h>
#include <platform/bcm28xx/pll.h>
#include <platform/bcm28xx/pll_read.h>
#include <platform/bcm28xx/power.h>
#include <platform/bcm28xx/udelay.h>
#include <stdio.h>

extern const uint8_t arm_chainloader_start[], arm_chainloader_end[];

static void prepare_usb_host(uint32_t revision) {
  unsigned int type = (revision >> 4) & 0xff;
  int lan_run = 0, ethclk_pin = 0;
  switch (type) {
  case 4: lan_run = 31; ethclk_pin = 44; break;
  case 8: lan_run = 29; ethclk_pin = 42; break;
  case 0xd: lan_run = 30; ethclk_pin = 42; break;
  }
  if (ethclk_pin > 0) {
    *REG32(CM_GP1CTL) = CM_PASSWORD | CM_GPnCTL_KILL_SET;
    while (*REG32(CM_GP1CTL) & CM_GPnCTL_BUSY_SET) { }
    uint32_t divisor = freq_pllc_per / (25000000 / 0x1000);
    *REG32(CM_GP1CTL) = CM_PASSWORD | (2 << CM_GPnCTL_MASH_LSB) | CM_SRC_PLLC_CORE0;
    *REG32(CM_GP1DIV) = CM_PASSWORD | divisor;
    *REG32(CM_GP1CTL) = CM_PASSWORD | (2 << CM_GPnCTL_MASH_LSB) | CM_SRC_PLLC_CORE0 | CM_GPnCTL_ENAB_SET;
    gpio_config(ethclk_pin, kBCM2708Pinmux_ALT0);
  }
  if (lan_run > 0) {
    gpio_config(lan_run, kBCM2708PinmuxOut);
    gpio_set(lan_run, 0);
    udelay(1000);
    gpio_set(lan_run, 1);
  }
}

static void __attribute__((optimize("-O1"))) arm_chainloader_init(uint level) {
  (void)level;
  uint32_t active, revision = otp_read(30), serial = otp_read(28);
  uint32_t uart_hz;
  size_t payload_size = (uintptr_t)arm_chainloader_end - (uintptr_t)arm_chainloader_start;
  /* The firmware-data producer currently describes a 1 GiB Pi 3B/3B+. */
  if (!arm_chainloader_board_supported(revision) || bcm_dsi7_framebuffer_handoff(&active)) {
    puts("ARM: unsupported board or display not ready; not releasing ARM");
    return;
  }
  arch_disable_ints();
  uart_hz = get_uart_base_freq();
  if (arm_chainloader_prepare_image((void *)0xc0000000u, 0x100000u,
                                    arm_chainloader_start, payload_size,
                                    serial, revision, uart_hz, active) ||
      arm_chainloader_configure_mappings(REG32(ARM_TRANSLATE), ARM_CHAINLOADER_MAP_ENTRIES)) {
    puts("ARM: chainloader preparation failed; not releasing ARM");
    return;
  }
  printf("ARM: payload=%u bytes framebuffer=0x%08x uart=%u Hz\n", payload_size, active, uart_hz);
  prepare_usb_host(revision);
  *REG32(ARM_CONTROL0) |= ARM_C0_BRESP2 | ARM_C0_SIZ1G | ARM_C0_APROTPASS |
                          ARM_C0_APROTMSK | ARM_C0_FULLPERI |
                          (0x8u << 20) | (0x5u << 24) | (0xau << 28);
  *REG32(ARM_CONTROL1) |= ARM_C1_PERSON;
  arch_disable_ints();
  if (arm_chainloader_setup_clock(1000)) {
    puts("ARM: clock initialization failed; not releasing ARM");
    return;
  }
  power_arm_start();
  puts("ARM: releasing chainloader");
  arm_chainloader_release_bridge();
}
LK_INIT_HOOK(arm_chainloader, &arm_chainloader_init, LK_INIT_LEVEL_PLATFORM + 10);
