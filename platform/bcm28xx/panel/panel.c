#include <stdint.h>
#include <dev/gpio.h>
#include <platform/bcm28xx/gpio.h>
#include <platform/bcm28xx/i2c.h>
#include <platform/bcm28xx/panel.h>
#include <platform/bcm28xx/udelay.h>
#include <stdio.h>

#define ATTINY_BUS 0
#define ATTINY_ADDR 0x45
#define ATTINY_REG_ID 0x80
#define ATTINY_REG_PORTA 0x81
#define ATTINY_REG_PORTB 0x82
#define ATTINY_REG_PORTC 0x83
#define ATTINY_REG_POWERON 0x85
#define ATTINY_REG_PWM 0x86
#define ATTINY_REG_REG_ADDR_L 0x8c
#define ATTINY_REG_REG_ADDR_H 0x8d
#define ATTINY_REG_REG_WRITE_DATA_H 0x90
#define ATTINY_REG_REG_WRITE_DATA_L 0x91

#define ATTINY_PA_LCD_LR (1u << 2)
#define ATTINY_PC_LED_EN (1u << 0)
#define ATTINY_PC_RST_LCD_N (1u << 2)
#define ATTINY_PC_RST_BRIDGE_N (1u << 3)

static int attiny_write(uint8_t reg, uint8_t value) {
  char data[2] = { (char)reg, (char)value };
  return i2c_xfer(ATTINY_BUS, ATTINY_ADDR, data, sizeof(data), NULL, 0);
}

static int attiny_read(uint8_t reg, uint8_t *value) {
  char command = (char)reg;
  char result = 0;
  int ret = i2c_xfer(ATTINY_BUS, ATTINY_ADDR, &command, 1, &result, 1);
  if (!ret)
    *value = (uint8_t)result;
  return ret;
}

int bcm_panel_attiny_init(void) {
  uint8_t id;
  /* Pi 3 DSI/CSI uses I2C0 on GPIO44/45, ALT1. */
  gpio_config(44, kBCM2708Pinmux_ALT1);
  gpio_config(45, kBCM2708Pinmux_ALT1);
  i2c_set_rate(ATTINY_BUS, 100000);
  if (attiny_read(ATTINY_REG_ID, &id)) {
    puts("ATTiny: read ID failed");
    return -1;
  }
  printf("ATTiny: ID 0x%02x\n", id);
  if (attiny_write(ATTINY_REG_POWERON, 0) || attiny_write(ATTINY_REG_PWM, 0))
    return -1;
  return 0;
}

int bcm_panel_attiny_power(int enable) {
  if (!enable) {
    if (attiny_write(ATTINY_REG_PWM, 0))
      return -1;
    udelay(5000);
    if (attiny_write(ATTINY_REG_PORTA, 0) ||
        attiny_write(ATTINY_REG_PORTB, 1u << 1) ||
        attiny_write(ATTINY_REG_PORTC, 0))
      return -1;
    udelay(30000);
    return 0;
  }
  if (attiny_write(ATTINY_REG_PORTC, 0))
    return -1;
  udelay(5000);
  if (attiny_write(ATTINY_REG_PORTA, ATTINY_PA_LCD_LR))
    return -1;
  udelay(5000);
  if (attiny_write(ATTINY_REG_PORTB, 1u << 7))
    return -1;
  udelay(5000);
  if (attiny_write(ATTINY_REG_PORTC, ATTINY_PC_LED_EN))
    return -1;
  udelay(80000);
  return 0;
}

int bcm_panel_attiny_bridge_power_on(void) {
  uint8_t portb = 0;
  udelay(100000);
  if (attiny_write(ATTINY_REG_POWERON, 1))
    return -1;
  udelay(25000);
  for (unsigned int i = 0; i < 100; ++i) {
    if (attiny_read(ATTINY_REG_PORTB, &portb))
      return -1;
    if (portb & 1u)
      return 0;
    udelay(1000);
  }
  /* The working panel can accept bridge writes without this status bit. */
  printf("ATTiny: bridge power not confirmed (PORTB=0x%02x)\n", portb);
  return 0;
}

int bcm_panel_attiny_set_backlight(uint8_t brightness) {
  return attiny_write(ATTINY_REG_PWM, brightness);
}

int bcm_panel_attiny_set_bridge_reset(int release) {
  uint8_t value = release ? ATTINY_PC_LED_EN | ATTINY_PC_RST_BRIDGE_N |
                            ATTINY_PC_RST_LCD_N : 0;
  if (attiny_write(ATTINY_REG_PORTC, value))
    return -1;
  if (!release)
    return 0;
  udelay(5000);
  /* Select TC358762 SYSPMCTRL through the ATTiny indirect interface. */
  if (attiny_write(ATTINY_REG_REG_ADDR_H, 0x04))
    return -1;
  udelay(5000);
  if (attiny_write(ATTINY_REG_REG_ADDR_L, 0x7c))
    return -1;
  udelay(5000);
  if (attiny_write(ATTINY_REG_REG_WRITE_DATA_H, 0x00) ||
      attiny_write(ATTINY_REG_REG_WRITE_DATA_L, 0x00))
    return -1;
  udelay(100000);
  return 0;
}
