#include <stdint.h>
#include <platform/bcm28xx/bridge.h>
#include <platform/bcm28xx/dsi.h>
#include <platform/bcm28xx/panel.h>
#include <platform/bcm28xx/udelay.h>

#define TC_PPI_STARTPPI 0x0104
#define TC_PPI_LPTXTIMECNT 0x0114
#define TC_PPI_D0S_ATMR 0x0144
#define TC_PPI_D1S_ATMR 0x0148
#define TC_PPI_D0S_CLRSIPOCOUNT 0x0164
#define TC_PPI_D1S_CLRSIPOCOUNT 0x0168
#define TC_DSI_STARTDSI 0x0204
#define TC_DSI_LANEENABLE 0x0210
#define TC_LCDCTRL 0x0420
#define TC_LCD_HS_HBP 0x0424
#define TC_LCD_HDISP_HFP 0x0428
#define TC_LCD_VS_VBP 0x042c
#define TC_LCD_VDISP_VFP 0x0430
#define TC_SPICMR 0x0450
#define TC_SYSCTRL 0x0464

static int tc358762_write(uint16_t address, uint32_t value) {
  uint8_t data[6] = {
    (uint8_t)address, (uint8_t)(address >> 8),
    (uint8_t)value, (uint8_t)(value >> 8),
    (uint8_t)(value >> 16), (uint8_t)(value >> 24),
  };
  return bcm_dsi1_generic_write(data, sizeof(data));
}

int bcm_tc358762_init(void) {
  /* The normal path uses the validated 800x480 legacy timing profile. */
  if (bcm_panel_attiny_init() || bcm_panel_attiny_power(1) ||
      bcm_panel_attiny_bridge_power_on() || bcm_panel_attiny_set_bridge_reset(1))
    return -1;

  if (tc358762_write(TC_DSI_LANEENABLE, 3) ||
      tc358762_write(TC_PPI_D0S_CLRSIPOCOUNT, 5) ||
      tc358762_write(TC_PPI_D1S_CLRSIPOCOUNT, 5) ||
      tc358762_write(TC_PPI_D0S_ATMR, 0) ||
      tc358762_write(TC_PPI_D1S_ATMR, 0) ||
      tc358762_write(TC_PPI_LPTXTIMECNT, 3) ||
      tc358762_write(TC_SPICMR, 0) ||
      tc358762_write(TC_LCDCTRL, 0x00100150u) ||
      tc358762_write(TC_SYSCTRL, 0x040f) ||
      tc358762_write(TC_LCD_HS_HBP, 2u | (46u << 16)) ||
      tc358762_write(TC_LCD_HDISP_HFP, 800u | (1u << 16)) ||
      tc358762_write(TC_LCD_VS_VBP, 2u | (21u << 16)) ||
      tc358762_write(TC_LCD_VDISP_VFP, 480u | (7u << 16)))
    return -1;

  udelay(100000);
  if (tc358762_write(TC_PPI_STARTPPI, 1) || tc358762_write(TC_DSI_STARTDSI, 1))
    return -1;
  udelay(100000);
  return 0;
}
