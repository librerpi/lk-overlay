#include <stdint.h>
#include <lk/reg.h>
#include <platform/bcm28xx.h>
#include <platform/bcm28xx/a2w.h>
#include <platform/bcm28xx/arm_chainloader.h>
#include <platform/bcm28xx/cm.h>

int arm_chainloader_setup_clock(uint32_t arm_mhz) {
  uint32_t dig0, dig1, dig2, dig3;
  if (!arm_mhz)
    return -1;

  /* Preserve the working BCM2837 PLLB sequence (19.2 MHz crystal, prediv 1). */
  uint64_t divisor = ((uint64_t)arm_mhz * 0x100000u * 10u) / 192u;
  uint32_t int_divisor = divisor >> 20;
  uint32_t frac_divisor = divisor & 0xfffff;
  *REG32(A2W_XOSC_CTRL) |= A2W_PASSWORD | A2W_XOSC_CTRL_PLLBEN_SET;
  *REG32(A2W_PLLB_FRAC) = A2W_PASSWORD | frac_divisor;
  *REG32(A2W_PLLB_CTRL) = A2W_PASSWORD | int_divisor | (1u << 12);
  *REG32(CM_PLLB) = CM_PASSWORD | CM_PLLB_DIGRST_SET | CM_PLL_ANARST_SET;
  *REG32(CM_PLLB) = CM_PASSWORD | CM_PLLB_DIGRST_SET | CM_PLL_ANARST_SET | CM_PLLB_HOLDARM_SET;
  *REG32(A2W_PLLB_ANA3) = A2W_PASSWORD | 0x100;
  *REG32(A2W_PLLB_ANA2) = A2W_PASSWORD;
  *REG32(A2W_PLLB_ANA1) = A2W_PASSWORD | 0x140000;
  *REG32(A2W_PLLB_ANA0) = A2W_PASSWORD;
  *REG32(CM_PLLB) = CM_PASSWORD | 0xaa;
  dig0 = *REG32(A2W_PLLB_DIG0);
  dig1 = *REG32(A2W_PLLB_DIG1);
  dig2 = *REG32(A2W_PLLB_DIG2);
  dig3 = *REG32(A2W_PLLB_DIG3);
  *REG32(A2W_PLLB_DIG3) = A2W_PASSWORD | dig3;
  *REG32(A2W_PLLB_DIG2) = A2W_PASSWORD | (dig2 & 0xffeffbfeu);
  *REG32(A2W_PLLB_DIG1) = A2W_PASSWORD | (dig1 & ~(1u << 14));
  *REG32(A2W_PLLB_DIG0) = A2W_PASSWORD | dig0;
  *REG32(A2W_PLLB_CTRL) |= A2W_PASSWORD | 0x20000;
  *REG32(A2W_PLLB_DIG3) = A2W_PASSWORD | (dig3 | 0x42);
  *REG32(A2W_PLLB_DIG2) = A2W_PASSWORD | dig2;
  *REG32(A2W_PLLB_DIG1) = A2W_PASSWORD | dig1;
  *REG32(A2W_PLLB_DIG0) = A2W_PASSWORD | dig0;
  *REG32(A2W_PLLB_ARM) = A2W_PASSWORD | 4;
  *REG32(CM_PLLB) = CM_PASSWORD | CM_PLLB_DIGRST_SET | CM_PLL_ANARST_SET |
                    CM_PLLB_HOLDARM_SET | CM_PLLB_LOADARM_SET;
  *REG32(CM_PLLB) = CM_PASSWORD | CM_PLLB_DIGRST_SET | CM_PLL_ANARST_SET | CM_PLLB_HOLDARM_SET;
  *REG32(CM_PLLB) = CM_PASSWORD;
  *REG32(CM_ARMCTL) = CM_PASSWORD | 4 | CM_ARMCTL_ENAB_SET;
  return 0;
}
