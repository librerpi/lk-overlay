#include <app.h>
#include <lib/console.h>
#include <lk/init.h>
#include <platform/bcm28xx/hvs.h>
#include <stdlib.h>

gfx_surface *gfx_grid;
hvs_layer *grid_layer;
int channel = PRIMARY_HVS_CHANNEL;

static void grid_init(uint level) {
  int width = 102;
  int height = 102;

  gfx_grid = gfx_create_surface(NULL, width, height, width, GFX_FORMAT_ARGB_8888);

  int grid = 20;
  for (int x=0; x< width; x++) {
    for (int y=0; y < height; y++) {
      uint color = 0x00000000;
      if (true) {
        if (y % grid == 0) color |= 0xffff0000;
        if (y % grid == 1) color |= 0xffff0000;
        if (x % grid == 0) color |= 0xff0000ff;
        if (x % grid == 1) color |= 0xff0000ff;
      }
      gfx_putpixel(gfx_grid, x, y, color);
    }
  }

  grid_layer = malloc(sizeof(hvs_layer));
  mk_unity_layer(grid_layer, gfx_grid, 60, 100, 100);
  grid_layer->name = "grid";
  grid_layer->w = 100;
  grid_layer->h = 300;

  mutex_acquire(&channels[channel].lock);
  hvs_dlist_add(channel, grid_layer);
  hvs_update_dlist(channel);
  mutex_release(&channels[channel].lock);
}

static void grid_entry(const struct app_descriptor *app, void *args) {
  bool grow_w = true;
  bool grow_h = true;

  thread_sleep(10 * 1000);

  thread_set_real_time(get_current_thread());
  thread_set_priority(HIGHEST_PRIORITY);

  while (true) {
    hvs_wait_vsync(channel);

    {
      if (grow_w && (grid_layer->w >= 300)) grow_w = false;
      if (!grow_w && (grid_layer->w <= 5)) grow_w = true;

      int delta = 1;
      if (!grow_w) delta = -1;

      grid_layer->w += delta;
    }
    {
      if (grow_h && (grid_layer->h >= 300)) grow_h = false;
      if (!grow_h && (grid_layer->h <= 5)) grow_h = true;

      int delta = 1;
      if (!grow_h) delta = -1;

      grid_layer->h += delta;
    }

    mutex_acquire(&channels[channel].lock);
    hvs_update_dlist(channel);
    mutex_release(&channels[channel].lock);

    //thread_sleep(1000 * 10);
  }
}

LK_INIT_HOOK(grid, &grid_init, LK_INIT_LEVEL_PLATFORM+1);

APP_START(grid)
  .entry = grid_entry,
APP_END
