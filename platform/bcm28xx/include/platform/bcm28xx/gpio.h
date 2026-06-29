#pragma once

#ifndef __ASSEMBLER__
#include <string.h>
#include <stdint.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

#ifndef __ASSEMBLER__
enum BCM2708PinmuxSetting {
  kBCM2708PinmuxIn = 0,
  kBCM2708PinmuxOut = 1,
  kBCM2708Pinmux_ALT5 = 2,
  kBCM2708Pinmux_ALT4 = 3,
  kBCM2708Pinmux_ALT0 = 4,
  kBCM2708Pinmux_ALT1 = 5,
  kBCM2708Pinmux_ALT2 = 6,
  kBCM2708Pinmux_ALT3 = 7
};

enum pull_mode {
  kPullOff = 0,
  kPullDown = 1,
  kPullUp = 2
};

#ifndef RPI4
struct gpio_pull_batch {
  uint32_t bank[3][2];
};

#define GPIO_PULL_CLEAR(batch) { bzero(&batch, sizeof(batch)); }
#define GPIO_PULL_SET(batch, pin, mode) { batch.bank[mode][pin / 32] |= 1 << (pin % 32); }

void gpio_apply_batch(struct gpio_pull_batch *batch);
#else
struct gpio_pull_batch {
  uint32_t bank[4];
  uint32_t mask[4];
};
#define GPIO_PULL_CLEAR(batch)
#define GPIO_PULL_SET(batch, pin, mode)
static inline void gpio_apply_batch(struct gpio_pull_batch *batch) {}
#endif

void gpio_set_pull(unsigned nr, enum pull_mode mode);

void gpio_register_irq(int nr);
#endif

/* GPIO */
#define GPIO_GPFSEL0   (GPIO_BASE + 0x00)
#define GPIO_GPFSEL1   (GPIO_BASE + 0x04)
#define GPIO_GPFSEL2   (GPIO_BASE + 0x08)
#define GPIO_GPFSEL3   (GPIO_BASE + 0x0C)
#define GPIO_GPFSEL4   (GPIO_BASE + 0x10)
#define GPIO_GPFSEL5   (GPIO_BASE + 0x14)
#define GPIO_GPSET0    (GPIO_BASE + 0x1C)
#define GPIO_GPSET1    (GPIO_BASE + 0x20)
#define GPIO_GPCLR0    (GPIO_BASE + 0x28)
#define GPIO_GPCLR1    (GPIO_BASE + 0x2C)
#define GPIO_GPLEV0    (GPIO_BASE + 0x34)
#define GPIO_GPLEV1    (GPIO_BASE + 0x38)
#define GPIO_GPEDS0    (GPIO_BASE + 0x40)
#define GPIO_GPEDS1    (GPIO_BASE + 0x44)
#define GPIO_GPREN0    (GPIO_BASE + 0x4C)
#define GPIO_GPREN1    (GPIO_BASE + 0x50)
#define GPIO_GPFEN0    (GPIO_BASE + 0x58)
#define GPIO_GPFEN1    (GPIO_BASE + 0x5C)
#define GPIO_GPHEN0    (GPIO_BASE + 0x64)
#define GPIO_GPHEN1    (GPIO_BASE + 0x68)
#define GPIO_GPLEN0    (GPIO_BASE + 0x70)
#define GPIO_GPLEN1    (GPIO_BASE + 0x74)
#define GPIO_GPAREN0   (GPIO_BASE + 0x7C)
#define GPIO_GPAREN1   (GPIO_BASE + 0x80)
#define GPIO_GPAFEN0   (GPIO_BASE + 0x88)
#define GPIO_GPAFEN1   (GPIO_BASE + 0x8C)
#define GPIO_GPPUD     (GPIO_BASE + 0x94)
#define GPIO_GPPUDCLK0 (GPIO_BASE + 0x98)
#define GPIO_GPPUDCLK1 (GPIO_BASE + 0x9C)
#define GPIO_2711_PULL (GPIO_BASE + 0xe4)
// 2 bits per reg, 16 pins per reg, 4 regs total
// 0=none, 1=up, 2=down

#ifdef __cplusplus
}
#endif
