#include <stddef.h>
#include <platform/bcm28xx/dsi_framebuffer.h>

int hvs_dsi_framebuffer_dlist(uint32_t *words, uint32_t physical) {
  if (!words || (physical != DSI_FB_PRIMARY_PHYS && physical != DSI_FB_BACK_PHYS))
    return -1;
  /* Valid, seven words, ABGR order, VFLIP, unity scaling, RGBA8888. */
  words[0] = (1u << 30) | (7u << 24) | (1u << 15) | (3u << 13) | (1u << 4) | 7u;
  words[1] = 0xffu << 24;
  words[2] = (DSI_FB_HEIGHT << 16) | DSI_FB_WIDTH;
  words[3] = 0xc0c0c0c0u;
  words[4] = 0xc0000000u | (physical + (DSI_FB_HEIGHT - 1) * DSI_FB_STRIDE_BYTES);
  words[5] = 0xc0c0c0c0u;
  words[6] = DSI_FB_STRIDE_BYTES;
  return 0;
}
