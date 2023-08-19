#include <app.h>
#include <stdio.h>
#include <lib/tga.h>
#include <platform/bcm28xx/hvs.h>
#include <string.h>

extern void *sprite_tga;
extern uint32_t sprite_tga_size;
gfx_surface *sprite_sheet;
static const int channel = PRIMARY_HVS_CHANNEL;

// https://www.spriters-resource.com/pc_computer/chipschallenge/sheet/27082/ main game window sprites
// https://www.spriters-resource.com/pc_computer/chipschallenge/sheet/27080/ main tiles

#define FLOOR 0,10
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

static void init_tile(screen_tile_t *t, int x, int y) {
  t->layer = malloc(sizeof(*t->layer));
  mk_unity_layer(t->layer, sprite_sheet, 60, 272, 714);
  hvs_allocate_premade(t->layer, 7);
  t->layer->x = 40 + (x * 34);
  t->layer->y = 40 + (y * 34);
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

static void tile_set_sprite(screen_tile_t *t, int x, int y) {
  t->layer->viewport_x = x * 34;
  t->layer->viewport_y = y * 34;
  hvs_regen_noscale_viewport_noalpha(t->layer);
}

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

  tile_set_sprite(&tiles[0][1], WALL);
  tile_set_sprite(&tiles[0][4], WALL);
  tile_set_sprite(&tiles[0][7], WALL);
  tile_set_sprite(&tiles[1][0], CHIP);
  tile_set_sprite(&tiles[1][1], WALL);
  tile_set_sprite(&tiles[1][2], BLUE_DOOR);
  tile_set_sprite(&tiles[1][3], WALL);
  tile_set_sprite(&tiles[1][4], WALL);
  tile_set_sprite(&tiles[1][5], WALL);
  tile_set_sprite(&tiles[1][6], RED_DOOR);
  tile_set_sprite(&tiles[1][7], WALL);
  tile_set_sprite(&tiles[1][8], WALL);
  tile_set_sprite(&tiles[2][1], GREEN_DOOR);
  tile_set_sprite(&tiles[2][3], BLUE_KEY_FLOOR);
  tile_set_sprite(&tiles[2][4], CHIP);
  tile_set_sprite(&tiles[2][5], BLUE_KEY_FLOOR);
  tile_set_sprite(&tiles[2][7], WALL);
  tile_set_sprite(&tiles[3][0], WALL);
  tile_set_sprite(&tiles[3][1], WALL);
  tile_set_sprite(&tiles[3][7], YELLOW_DOOR);
  tile_set_sprite(&tiles[4][0], EXIT);
  tile_set_sprite(&tiles[4][1], SOCKET);
  tile_set_sprite(&tiles[4][6], CHIP);
  tile_set_sprite(&tiles[4][7], WALL);
  tile_set_sprite(&tiles[4][8], WALL);
  tile_set_sprite(&tiles[5][0], WALL);
  tile_set_sprite(&tiles[5][1], WALL);
  tile_set_sprite(&tiles[5][7], YELLOW_DOOR);
  tile_set_sprite(&tiles[6][1], GREEN_DOOR);
  tile_set_sprite(&tiles[6][3], RED_KEY_FLOOR);
  tile_set_sprite(&tiles[6][4], CHIP);
  tile_set_sprite(&tiles[6][5], RED_KEY_FLOOR);
  tile_set_sprite(&tiles[6][7], WALL);
  tile_set_sprite(&tiles[7][0], CHIP);
  tile_set_sprite(&tiles[7][1], WALL);
  tile_set_sprite(&tiles[7][2], RED_DOOR);
  tile_set_sprite(&tiles[7][3], WALL);
  tile_set_sprite(&tiles[7][4], WALL);
  tile_set_sprite(&tiles[7][5], WALL);
  tile_set_sprite(&tiles[7][6], BLUE_DOOR);
  tile_set_sprite(&tiles[7][7], WALL);
  tile_set_sprite(&tiles[7][8], WALL);
  tile_set_sprite(&tiles[8][1], WALL);
  tile_set_sprite(&tiles[8][4], WALL);
  tile_set_sprite(&tiles[8][7], WALL);
  hvs_update_dlist(channel);
  mutex_release(&channels[channel].lock);

}

APP_START(game)
  .entry = game_entry,
APP_END
