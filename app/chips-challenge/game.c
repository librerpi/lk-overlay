#include <app.h>
#include <lib/tga.h>
#include <lk/console_cmd.h>
#include <platform/bcm28xx/hvs.h>
#include <platform/bcm28xx/print_timestamp.h>
#include <stdio.h>
#include <string.h>

extern void *sprite_tga;
extern uint32_t sprite_tga_size;
gfx_surface *sprite_sheet;
static const int channel = PRIMARY_HVS_CHANNEL;

extern uint8_t *map_data;
extern uint32_t level_count;

#define logf(fmt, ...) { print_timestamp(); printf("[game:%s:%d]: "fmt, __FUNCTION__, __LINE__, ##__VA_ARGS__); }

// https://www.spriters-resource.com/pc_computer/chipschallenge/sheet/27082/ main game window sprites
// https://www.spriters-resource.com/pc_computer/chipschallenge/sheet/27080/ main tiles

#define FLOOR 80
#define WALL 0,12
#define EXIT 2,15
#define CHIP 1,10
#define SOCKET 2,10
#define RED_DOOR          4,12
#define BLUE_DOOR         5,12
#define YELLOW_DOOR       6,12
#define GREEN_DOOR        7,12
#define RED_KEY_FLOOR    0,2
#define BLUE_KEY_FLOOR   1,2

typedef struct {
  hvs_layer *layer;
} screen_tile_t;

screen_tile_t tiles[9][9];

// x left<->right
// y up<->down

static void init_tile(screen_tile_t *t, int x, int y) {
  t->layer = malloc(sizeof(*t->layer));
  mk_unity_layer(t->layer, sprite_sheet, 60, 272, 714);
  hvs_allocate_premade(t->layer, 7);
  t->layer->x = 50 + (x * 34);
  t->layer->y = 60 + (y * 34);
  t->layer->w = 272;
  t->layer->h = 714;
  t->layer->viewport_x = 0;
  t->layer->viewport_y = 0;
  t->layer->viewport_w = 34;
  t->layer->viewport_h = 34;
  t->layer->fb = sprite_sheet;
  t->layer->name = strdup("sprite sheet");
  hvs_regen_noscale_viewport_noalpha(t->layer);
  hvs_dlist_add(channel, t->layer);
}

static void tile_set_sprite(screen_tile_t *t, int tile) {
  t->layer->viewport_x = (tile%8) * 34;
  t->layer->viewport_y = (tile/8) * 34;
  hvs_regen_noscale_viewport_noalpha(t->layer);
}

static int level = 0;
static int camera_x = 0;
static int camera_y = 0;

static void redraw_map(void) {
  for (int j=0; j<9; j++) {
    const int y = j + camera_y;
    for (int i=0; i<9; i++) {
      const int x = i + camera_x;
      uint8_t tile = map_data[(level * 1024) + (y * 32) + x];
      tile_set_sprite(&tiles[i][j], tile-1);
    }
  }
}

static int cmd_move(int argc, const console_cmd_args *argv) {
  if (strcmp(argv[0].str, "down") == 0) {
    camera_y++;
  }
  if (strcmp(argv[0].str, "up") == 0) {
    camera_y--;
  }
  if (strcmp(argv[0].str, "left") == 0) {
    camera_x--;
  }
  if (strcmp(argv[0].str, "right") == 0) {
    camera_x++;
  }
  if (camera_x < 0) camera_x = 0;
  else if (camera_x > (32-9)) camera_x = 32-9;

  if (camera_y < 0) camera_y = 0;
  else if (camera_y > (32-9)) camera_y = 32-9;

  mutex_acquire(&channels[channel].lock);
  redraw_map();
  hvs_update_dlist(channel);
  mutex_release(&channels[channel].lock);
  return 0;
}

STATIC_COMMAND_START
STATIC_COMMAND("up", "", cmd_move)
STATIC_COMMAND("down", "", cmd_move)
STATIC_COMMAND("left", "", cmd_move)
STATIC_COMMAND("right", "", cmd_move)
STATIC_COMMAND_END(game);

static void game_entry(const struct app_descriptor *app, void *args) {
  printf("%p + %d\n", sprite_tga, sprite_tga_size);
  sprite_sheet = tga_decode(sprite_tga, sprite_tga_size, GFX_FORMAT_ARGB_8888);
  printf("%p\n", sprite_sheet);
  if (!sprite_sheet) return;

  mutex_acquire(&channels[channel].lock);
  for (int i=0; i<9; i++) {
    for (int j=0; j<9; j++) {
      init_tile(&tiles[i][j], i, j);
    }
  }

  for (int i=0; i<9; i++) {
    for (int j=0; j<9; j++) {
      tile_set_sprite(&tiles[i][j], FLOOR);
    }
  }

  printf("%d levels at %p\n", level_count, map_data);

  redraw_map();
  hvs_update_dlist(channel);
  mutex_release(&channels[channel].lock);

}

APP_START(game)
  .entry = game_entry,
APP_END
