/*
 * VideoCore4_Drivers
 * Copyright (c) 2017 Authors of rpi-open-firmware
 *
 * USB PHY initialization driver.
 */

#include <lk/init.h>
#include <lk/reg.h>
#include <platform/bcm28xx.h>
#include <stdio.h>
#include <platform/bcm28xx/udelay.h>

#define USB_GUSBCFG   0x7e98000c
#define USB_GUSBCFG_USB_TRD_TIM_LSB         10
#define USB_GUSBCFG_SRP_CAP_SET             0x00000100
#define USB_GUSBCFG_HNP_CAP_SET             0x00000200
#define USB_GUSBCFG_TERM_SEL_DL_PULSE_SET   0x00400000
#define USB_GUSBCFG_FORCE_DEV_MODE_SET      0x40000000
#define USB_GMDIOCSR  0x7e980080
#define FLAG_BUSY       BV(31)
#define USB_GMDIOGEN  0x7e980084
#define USB_GVBUSDRV  0x7e980088
#define USB_HCFG 0x7e980400
#define USB_HFIR 0x7e980404


static void wait(void) {
  while (*REG32(USB_GMDIOCSR) & FLAG_BUSY);
}

void write_bare(int reg, uint16_t value, int type) {
  reg &= 0x1F;

  /* precede MDIO access */
  *REG32(USB_GMDIOGEN) = 0xFFFFFFFF;
  wait();

  /* write the actual value, with flags */
  *REG32(USB_GMDIOGEN) = type | (reg << 18) | value;
  wait();

  /* dummy write due to errata; see BCM2835 peripheral manual */
  *REG32(USB_GMDIOGEN) = 0;
  wait();
}

static uint16_t usb_read(int reg) {
  write_bare(reg, 0, 0x60020000);
  return *REG32(USB_GMDIOCSR) & 0xFFFFF;
}

static void usb_write(int reg, uint16_t value) {
  write_bare(reg, value, 0x50020000);
}

static void usbphy_init(uint level) {
  int devmode = 0;

  puts("bringing up usb PHY...");

  *REG32(USB_GMDIOCSR) = BV(18);

  usb_write(0x15, devmode ? 4369 : 272);
  usb_write(0x19, 0x4);
  usb_write(0x18, devmode ? 0x342 : 0x10);
  usb_write(0x1D, 0x4);
  usb_write(0x17, 5682); // based on crystal speed

  while((usb_read(0x1B) & (1 << 7)) != 0);

  *REG32(USB_GVBUSDRV) &= ~BV(7);

  usb_write(0x1E, 0x8000);

  usb_write(0x1D, 0x5000);
  usb_write(0x19, 0xC004);
  usb_write(0x20, 0x1C2F);
  usb_write(0x22, 0x0100);
  usb_write(0x24, 0x0010);
  usb_write(0x19, 0x0004);

  *REG32(USB_GVBUSDRV) = (*REG32(USB_GVBUSDRV) & 0xFFF0FFFF) | 0xD0000; // axi priority
  udelay(300);
  if (devmode) {
    *REG32(USB_GUSBCFG) =
      USB_GUSBCFG_FORCE_DEV_MODE_SET |
      USB_GUSBCFG_TERM_SEL_DL_PULSE_SET |
      (9 << USB_GUSBCFG_USB_TRD_TIM_LSB) |
      USB_GUSBCFG_HNP_CAP_SET |
      USB_GUSBCFG_SRP_CAP_SET;
  } else {
    *REG32(0x7E980400 + 3084) = 0x20402700; // USB_HCFG + something
    udelay(300);
    *REG32(USB_HCFG) = 1;
    udelay(300);
    *REG32(USB_HFIR) = 0xBB80; // 48mhz / 0xBB80 == 1ms frame interval
    udelay(300);
  }
  puts("usb PHY up");
}

LK_INIT_HOOK(usbphy, &usbphy_init, LK_INIT_LEVEL_PLATFORM + 5);
