#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <platform/bcm28xx/dsi_framebuffer.h>

int main(void) {
  const uint32_t primary[] = {0x4700e017, 0xff000000, 0x01e00320,
                              0xc0c0c0c0, 0xc1976380, 0xc0c0c0c0, 3200};
  const uint32_t back[] = {0x4700e017, 0xff000000, 0x01e00320,
                           0xc0c0c0c0, 0xc1aed380, 0xc0c0c0c0, 3200};
  uint32_t words[DSI_FB_DLIST_WORDS];
  assert(hvs_dsi_framebuffer_dlist(words, DSI_FB_PRIMARY_PHYS) == 0);
  assert(memcmp(words, primary, sizeof(words)) == 0);
  assert(hvs_dsi_framebuffer_dlist(words, DSI_FB_BACK_PHYS) == 0);
  assert(memcmp(words, back, sizeof(words)) == 0);
  assert(hvs_dsi_framebuffer_dlist(words, DSI_FB_BACK_PHYS + 4) == -1);
  assert(memcmp(words, back, sizeof(words)) == 0);
  assert(hvs_dsi_framebuffer_dlist(NULL, DSI_FB_PRIMARY_PHYS) == -1);
  puts("DSI framebuffer display-list tests passed");
  return 0;
}
