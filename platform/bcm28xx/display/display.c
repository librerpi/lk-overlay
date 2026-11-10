#include <kernel/mutex.h>
#include <lib/gfx.h>
#include <lk/init.h>
#include <platform/bcm28xx.h>
#include <platform/bcm28xx/bridge.h>
#include <platform/bcm28xx/display.h>
#include <platform/bcm28xx/dsi.h>
#include <platform/bcm28xx/dsi_framebuffer.h>
#include <platform/bcm28xx/hvs.h>
#include <platform/bcm28xx/panel.h>
#include <platform/bcm28xx/pv.h>
#include <platform/bcm28xx/udelay.h>
#include <stdio.h>
#include <stdlib.h>

static gfx_surface *primary_fb;
static gfx_surface *back_fb;
static hvs_layer *display_layer;
static bool scanout_ready;

static const struct pv_timings dsi7_timings = {
  .hfp = 1, .hsync = 2, .hbp = 46, .hactive = DSI_FB_WIDTH,
  .vfp = 7, .vsync = 2, .vbp = 21, .vactive = DSI_FB_HEIGHT,
  .interlaced = false, .clock_mux = clk_dsi,
};

int bcm_dsi7_framebuffer_handoff(uint32_t *physical) {
  if (!physical || !scanout_ready || !display_layer || !display_layer->fb)
    return -1;
  *physical = (uint32_t)(uintptr_t)display_layer->fb->ptr & 0x3fffffffu;
  return (*physical == DSI_FB_PRIMARY_PHYS || *physical == DSI_FB_BACK_PHYS) ? 0 : -1;
}

static int prepare_framebuffer(void) {
  if (display_layer)
    return 0;
  primary_fb = gfx_create_surface((void *)(uintptr_t)(BCM_SDRAM_BUS_ADDR_BASE_NO_L2 | DSI_FB_PRIMARY_PHYS),
                                  DSI_FB_WIDTH, DSI_FB_HEIGHT, DSI_FB_WIDTH, GFX_FORMAT_ARGB_8888);
  back_fb = gfx_create_surface((void *)(uintptr_t)(BCM_SDRAM_BUS_ADDR_BASE_NO_L2 | DSI_FB_BACK_PHYS),
                               DSI_FB_WIDTH, DSI_FB_HEIGHT, DSI_FB_WIDTH, GFX_FORMAT_ARGB_8888);
  if (!primary_fb || !back_fb)
    return -1;
  primary_fb->free_on_destroy = false;
  back_fb->free_on_destroy = false;
  display_layer = malloc(sizeof(*display_layer));
  if (!display_layer)
    return -1;
  mk_unity_layer(display_layer, primary_fb, 10, 0, 0);
  display_layer->name = NULL;
  mutex_acquire(&channels[HVS_DSI1_CHANNEL].lock);
  hvs_dlist_add(HVS_DSI1_CHANNEL, display_layer);
  mutex_release(&channels[HVS_DSI1_CHANNEL].lock);
  return 0;
}

static int prepare_scanout(void) {
  static const uint32_t colors[] = {
    0xffffffff, 0xffffff00, 0xff00ffff, 0xff00ff00,
    0xffff00ff, 0xffff0000, 0xff0000ff, 0xff000000,
  };
  if (prepare_framebuffer())
    return -1;
  for (unsigned int bar = 0; bar < 8; ++bar)
    gfx_fillrect(back_fb, bar * 100u, 0, 100u, DSI_FB_HEIGHT, colors[bar]);
  gfx_flush(back_fb);
  display_layer->fb = back_fb;
  hvs_configure_channel_dsi1(DSI_FB_WIDTH, DSI_FB_HEIGHT);
  mutex_acquire(&channels[HVS_DSI1_CHANNEL].lock);
  hvs_update_dlist(HVS_DSI1_CHANNEL);
  mutex_release(&channels[HVS_DSI1_CHANNEL].lock);
  hvs_start_channel_dsi1(DSI_FB_WIDTH, DSI_FB_HEIGHT);
  return 0;
}

int bcm_dsi7_scanout(void) {
  struct bcm_dsi_clock_config cfg = { .pixel_hz_requested = 25979400u };
  struct pixel_valve *pv = getPvAddr(1);

  scanout_ready = false;
  bcm_dsi1_write(BCM_DSI1_DISP0_CTRL, 0);
  if (pv) pv->c = 0;
  hvs_channels[HVS_DSI1_CHANNEL].dispctrl = 0;
  udelay(1000);
  if (bcm_dsi1_clock_init(&cfg)) {
    puts("DSI: clock initialization failed");
    return -1;
  }
  if (bcm_dsi1_phy_init(&cfg)) {
    puts("DSI: PHY initialization failed");
    return -1;
  }
  if (bcm_dsi1_video_prepare())
    return -1;
  hvs_initialize();
  setup_pixelvalve(&dsi7_timings, 1);
  if (prepare_scanout() || bcm_tc358762_init() || bcm_dsi1_video_enable() ||
      bcm_panel_attiny_set_backlight(255))
    return -1;
  enable_pixelvalve(1);
  udelay(49000);
  scanout_ready = true;
  puts("DSI: 800x480 color bars active");
  return 0;
}

static void dsi7_init_hook(uint level) {
  (void)level;
  if (bcm_dsi7_scanout())
    puts("DSI: display initialization failed");
}
LK_INIT_HOOK(dsi7_display, &dsi7_init_hook, LK_INIT_LEVEL_PLATFORM + 1);
