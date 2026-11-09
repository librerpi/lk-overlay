#pragma once

#include <stdint.h>

#define DSI_FB_PRIMARY_PHYS 0x01800000u
#define DSI_FB_BACK_PHYS    0x01977000u
#define DSI_FB_POOL_BASE    0x01800000u
#define DSI_FB_POOL_SIZE    0x00300000u
#define DSI_FB_WIDTH 800u
#define DSI_FB_HEIGHT 480u
#define DSI_FB_STRIDE_BYTES 3200u
#define DSI_FB_DLIST_WORDS 7u

/* Build the working 800x480 ARGB8888 VFLIP scanout list. */
int hvs_dsi_framebuffer_dlist(uint32_t *words, uint32_t physical);
