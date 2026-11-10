#pragma once

#include <stddef.h>
#include <stdint.h>

#define BCM_DSI1_CTRL           0x00
#define BCM_DSI1_TXPKT1C        0x04
#define BCM_DSI1_TXPKT1H        0x08
#define BCM_DSI1_TXPKT_CMD_FIFO 0x1c
#define BCM_DSI1_TXPKT_PIX_FIFO 0x20
#define BCM_DSI1_DISP0_CTRL     0x28
#define BCM_DSI1_DISP1_CTRL     0x2c
#define BCM_DSI1_INT_STAT       0x30
#define BCM_DSI1_INT_EN         0x34
#define BCM_DSI1_STAT           0x38
#define BCM_DSI1_HSTX_TO_CNT    0x3c
#define BCM_DSI1_LPRX_TO_CNT    0x40
#define BCM_DSI1_TA_TO_CNT      0x44
#define BCM_DSI1_PR_TO_CNT      0x48
#define BCM_DSI1_PHYC           0x4c
#define BCM_DSI1_HS_CLT0        0x50
#define BCM_DSI1_HS_CLT1        0x54
#define BCM_DSI1_HS_CLT2        0x58
#define BCM_DSI1_HS_DLT3        0x5c
#define BCM_DSI1_HS_DLT4        0x60
#define BCM_DSI1_HS_DLT5        0x64
#define BCM_DSI1_HS_DLT6        0x68
#define BCM_DSI1_HS_DLT7        0x6c
#define BCM_DSI1_PHY_AFEC0      0x70
#define BCM_DSI1_PHY_AFEC1      0x74

struct bcm_dsi_clock_config {
  uint32_t pixel_hz_requested;
  uint32_t pixel_hz_actual;
  uint32_t phy_hz;
  uint32_t escape_hz;
};

uint32_t bcm_dsi1_read(uint32_t offset);
void bcm_dsi1_write(uint32_t offset, uint32_t value);
int bcm_dsi1_clock_init(struct bcm_dsi_clock_config *cfg);
int bcm_dsi1_phy_init(const struct bcm_dsi_clock_config *cfg);
int bcm_dsi1_video_prepare(void);
int bcm_dsi1_video_enable(void);

/* Send a generic DSI write packet to the panel bridge. */
int bcm_dsi1_generic_write(const void *data, size_t len);
