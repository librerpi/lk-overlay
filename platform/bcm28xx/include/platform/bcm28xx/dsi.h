#pragma once

#include <stddef.h>
#include <stdint.h>

#define BCM_DSI1_CTRL           0x00
#define BCM_DSI1_TXPKT1C        0x04
#define BCM_DSI1_TXPKT1H        0x08
#define BCM_DSI1_TXPKT_CMD_FIFO 0x1c
#define BCM_DSI1_TXPKT_PIX_FIFO 0x20
#define BCM_DSI1_DISP0_CTRL     0x28
#define BCM_DSI1_INT_STAT       0x30
#define BCM_DSI1_INT_EN         0x34
#define BCM_DSI1_STAT           0x38

uint32_t bcm_dsi1_read(uint32_t offset);
void bcm_dsi1_write(uint32_t offset, uint32_t value);

/* Send a generic DSI write packet to the panel bridge. */
int bcm_dsi1_generic_write(const void *data, size_t len);
