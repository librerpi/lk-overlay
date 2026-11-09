#include <platform/bcm28xx/dsi.h>
#include <platform/bcm28xx/udelay.h>

#define DSI_CTRL_RESET_FIFOS ((1u << 7) | (1u << 6) | (1u << 5) | (1u << 4))
#define DSI_DISP0_ENABLE (1u << 0)
#define DSI_TXPKT1C_CMD_REPEAT (1u << 10)
#define DSI_TXPKT1C_CMD_MODE_LP (1u << 3)
#define DSI_TXPKT1C_CMD_TYPE_LONG (1u << 2)
#define DSI_TXPKT1C_CMD_EN (1u << 0)
#define DSI_TXPKT1C_DISPLAY_SECONDARY (2u << 8)
#define DSI_INT_TXPKT1_DONE (1u << 1)
#define DSI_INT_TXPKT1_END (1u << 0)
#define DSI_INT_ERROR_MASK ((1u << 22) | (0xfu << 6) | (0x3u << 8) | (0xfu << 10))

static void dsi1_reset_fifos(void) {
  uint32_t ctrl = bcm_dsi1_read(BCM_DSI1_CTRL);
  uint32_t pktc = bcm_dsi1_read(BCM_DSI1_TXPKT1C);
  bcm_dsi1_write(BCM_DSI1_TXPKT1C, pktc & ~DSI_TXPKT1C_CMD_EN);
  udelay(1);
  bcm_dsi1_write(BCM_DSI1_CTRL, ctrl | DSI_CTRL_RESET_FIFOS);
  bcm_dsi1_write(BCM_DSI1_TXPKT1C, 0);
  bcm_dsi1_write(BCM_DSI1_INT_EN, DSI_INT_ERROR_MASK);
}

int bcm_dsi1_generic_write(const void *data, size_t len) {
  const uint8_t *payload = data;
  uint32_t pkth;
  uint32_t pktc = DSI_TXPKT1C_CMD_REPEAT | DSI_TXPKT1C_CMD_MODE_LP | DSI_TXPKT1C_CMD_EN;
  size_t cmd_len, pix_len;
  static int recovering;
  unsigned int timeout = 500000;

  if (!data || !len || len > 1024)
    return -1;
  if (len == 1) {
    pkth = 0x13 | ((uint32_t)payload[0] << 8);
    cmd_len = pix_len = 0;
  } else if (len == 2) {
    pkth = 0x23 | ((uint32_t)payload[0] << 8) | ((uint32_t)payload[1] << 16);
    cmd_len = pix_len = 0;
  } else {
    cmd_len = len <= 16 ? len : len % 4;
    pix_len = (len - cmd_len) / 4;
    if (pix_len >= 256)
      return -1;
    pkth = 0x29 | ((uint32_t)len << 8) | ((uint32_t)cmd_len << 24);
    pktc |= DSI_TXPKT1C_CMD_TYPE_LONG;
    if (pix_len)
      pktc |= DSI_TXPKT1C_DISPLAY_SECONDARY;
  }
  for (size_t i = 0; i < cmd_len; ++i)
    bcm_dsi1_write(BCM_DSI1_TXPKT_CMD_FIFO, payload[i]);
  for (size_t i = 0; i < pix_len; ++i) {
    const uint8_t *p = payload + cmd_len + i * 4;
    bcm_dsi1_write(BCM_DSI1_TXPKT_PIX_FIFO, p[0] | ((uint32_t)p[1] << 8) |
                   ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24));
  }
  bcm_dsi1_write(BCM_DSI1_INT_STAT, DSI_INT_TXPKT1_DONE | DSI_INT_TXPKT1_END | DSI_INT_ERROR_MASK);
  bcm_dsi1_write(BCM_DSI1_INT_EN, DSI_INT_ERROR_MASK | DSI_INT_TXPKT1_DONE);
  bcm_dsi1_write(BCM_DSI1_TXPKT1H, pkth);
  bcm_dsi1_write(BCM_DSI1_TXPKT1C, pktc);
  while (timeout--) {
    uint32_t stat = bcm_dsi1_read(BCM_DSI1_INT_STAT);
    if (stat & (DSI_INT_TXPKT1_DONE | DSI_INT_TXPKT1_END)) {
      bcm_dsi1_write(BCM_DSI1_INT_STAT, DSI_INT_TXPKT1_DONE | DSI_INT_TXPKT1_END | DSI_INT_ERROR_MASK);
      return 0;
    }
    if (stat & DSI_INT_ERROR_MASK) {
      dsi1_reset_fifos();
      return -1;
    }
    udelay(1);
  }
  dsi1_reset_fifos();
  uint32_t disp0 = bcm_dsi1_read(BCM_DSI1_DISP0_CTRL);
  if (!recovering && !(disp0 & DSI_DISP0_ENABLE)) {
    recovering = 1;
    bcm_dsi1_write(BCM_DSI1_DISP0_CTRL, disp0 | DSI_DISP0_ENABLE);
    udelay(30000);
    bcm_dsi1_write(BCM_DSI1_DISP0_CTRL, disp0);
    udelay(30000);
    int ret = bcm_dsi1_generic_write(data, len);
    recovering = 0;
    return ret;
  }
  return -1;
}
