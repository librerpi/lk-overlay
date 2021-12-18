#include <app.h>
#include <arch/ops.h>
#include <assert.h>
#include <dance.h>
#include <lib/tga.h>
#include <lk/console_cmd.h>
#include <lk/err.h>
#include <lk/reg.h>
#include <platform/bcm28xx/clock.h>
#include <platform/bcm28xx/hvs.h>
#include <platform/bcm28xx/pv.h>
#include <platform/mailbox.h>
#include <rand.h>
#include <stdio.h>
#include <stdlib.h>

static int cmd_hvs_dance(int argc, const console_cmd_args *argv);
static int cmd_hvs_limit(int argc, const console_cmd_args *argv);
static int cmd_hvs_delay(int argc, const console_cmd_args *argv);
static int cmd_dance_update(int argc, const console_cmd_args *argv);
static int cmd_dance_list(int argc, const console_cmd_args *argv);
static void update_visibility(int channel);

STATIC_COMMAND_START
STATIC_COMMAND("dance", "make the HVS dance in another direction", &cmd_hvs_dance)
STATIC_COMMAND("l", "limit sprites", &cmd_hvs_limit)
STATIC_COMMAND("d", "delay updates", &cmd_hvs_delay)
STATIC_COMMAND("u", "update", &cmd_dance_update)
STATIC_COMMAND("dance_list", "list", &cmd_dance_list)
STATIC_COMMAND_END(hvs_dance);

gfx_surface *fb;

struct item {
  signed int xd, yd;
  bool visible;
  hvs_layer layer;
};

#define ITEMS 580
struct item items[ITEMS];
uint32_t sprite_limit = 1;
int delay = 1;

int32_t screen_width, screen_height;

#define SCALED

void do_frame_update(int frame) {
  if (delay != 0) {
    if ((frame % delay) != 0) return;
  }
  //if ((display_slot + (sprite_limit * 14) + 1) > 4096) {
  //  printf("early dlist loop %d\n", display_slot);
  //  display_slot = 0;
  //}

  for (unsigned int i=0; i < sprite_limit; i++) {
    struct item *it = &items[i];
    int w = it->layer.w;
    int h = it->layer.h;

    if ((it->layer.x + it->xd) < 0) {
      it->layer.x += it->xd;
      it->layer.x *= -1;
      it->xd *= -1;
    } else if ((it->layer.x + w) > screen_width) {
      it->layer.x -= (it->layer.x + w) - screen_width;
      it->xd *= -1;
    } else {
      it->layer.x += it->xd;
    }

    if ((it->layer.y + it->yd) < 0) {
      it->layer.y += it->yd;
      it->layer.y *= -1;
      it->yd *= -1;
    } else if ((it->layer.y + h) > screen_height) {
      it->layer.y -= (it->layer.y + h) - screen_height;
      it->yd *= -1;
    } else {
      it->layer.y += it->yd;
    }
  }

  int hvs_channel = 1;
  mutex_acquire(&channels[hvs_channel].lock);
  hvs_update_dlist(hvs_channel);
  mutex_release(&channels[hvs_channel].lock);
}

static int cmd_dance_list(int argc, const console_cmd_args *argv) {
  for (unsigned int i=0; i < sprite_limit; i++) {
    struct item *it = &items[i];
    printf("%d: %dx%d+%dx%d, rate %dx%d\n", i, it->layer.x, it->layer.y, it->layer.w, it->layer.h, it->xd, it->yd);
  }
  return 0;
}

static int cmd_dance_update(int argc, const console_cmd_args *argv) {
  do_frame_update(0);
  return 0;
}

static void dance_scramble(void) {
  int w,h;
#ifdef SCALED
  w = 100;
  h = 100;
#else
  w = fb->width;
  h = fb->height;
#endif
  for (int i=0; i<ITEMS; i++) {
    struct item *it = &items[i];
    it->layer.x = (unsigned int)rand() % screen_width;
    it->layer.y = (unsigned int)rand() % screen_height;
    it->xd = (rand() % 5)+1;
    it->yd = (rand() % 5)+1;
    if (it->layer.x > (screen_width - w)) it->layer.x = screen_width - fb->width;
    if (it->layer.y > (screen_height - h)) it->layer.y = screen_height - fb->height;
  }

  //items[0].layer.x = 50;
  //items[0].layer.y = 50;
  //items[0].xd = 1;
  //items[0].yd = 1;

#if 0
  items[1].x = 140;
  items[1].y = 0;
  items[1].xd = 0;
  items[1].yd = 1;
#endif
}

static int cmd_hvs_dance(int argc, const console_cmd_args *argv) {
  dance_scramble();
  return 0;
}

static int cmd_hvs_limit(int argc, const console_cmd_args *argv) {
  if (argc == 2) sprite_limit = argv[1].u;
  if (sprite_limit > ITEMS) sprite_limit = ITEMS;

  int channel = 1;
  update_visibility(channel);
  return 0;
}

static void update_visibility(int channel) {
  mutex_acquire(&channels[channel].lock);
  for (unsigned int i=0; i<ITEMS; i++) {
    struct item *it = &items[i];
    if ((i < sprite_limit) && !it->visible) {
      //list_add_tail(&channels[channel].layers, &it->layer.node);
      hvs_dlist_add(channel, &it->layer);
      it->visible = true;
    } else if (it->visible && (i >= sprite_limit)) {
      list_delete(&it->layer.node);
      it->visible = false;
    }
  }
  mutex_release(&channels[channel].lock);
}

static int cmd_hvs_delay(int argc, const console_cmd_args *argv) {
  if (argc == 2) delay = argv[1].u;
  return 0;
}

void dance_start(gfx_surface* fbin, int hvs_channel) {
  fb = fbin;

  //hvs_set_background_color(hvs_channel, 0xffffff);
  screen_width = (hvs_channels[hvs_channel].dispctrl >> 12) & 0xfff;
  screen_height = (hvs_channels[hvs_channel].dispctrl & 0xfff);
  printf("detected a %dx%d screen\n", screen_width, screen_height);
  for (int i=0; i<ITEMS; i++) {
    struct item *it = &items[i];
    it->visible = false;
    mk_unity_layer(&it->layer, fbin, i, 150, 150);
    it->layer.name = malloc(10);
    snprintf(it->layer.name, 10, "sprite %d", i);
    it->layer.w /= 4;
    it->layer.h /= 4;
  }
  update_visibility(hvs_channel);

  srand(*REG32(ST_CLO));
  dance_scramble();
}

static void dance_init(const struct app_descriptor *app) {
#if 0
  if (!fb) {
    fb = gfx_create_surface(NULL, 10, 10, 10, GFX_FORMAT_ARGB_8888);
    for (unsigned int i=0; i<fb->width; i++) {
      int r = 0;
      if ((i%2) == 0) r = 255;
      for (unsigned int j=0; j<fb->height; j++) {
        int g = 0;
        if ((j%2) == 0) g = 255;
        gfx_putpixel(fb, i, j, 0xff000000 | (g << 8) | r);
      }
    }
  }
  fb->flush = 0;
#endif
  //screen_width = 0x500;
  //screen_height = 0x400;
}

static void dance_entry(const struct app_descriptor *app, void *args) {
  int hvs_channel = 1;
  while (true) {
    uint32_t stat = hvs_wait_vsync(hvs_channel);

    //uint32_t line = SCALER_STAT_LINE(stat);
    uint32_t frame = (stat >> 12) & 0x3f;
    //printf("frame %d\n", frame);
    do_frame_update(frame);
  }
}

APP_START(hvs_dance)
  .init = dance_init,
  .entry = dance_entry,
APP_END
