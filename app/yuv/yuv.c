#include <app.h>
#include <kernel/timer.h>
#include <lib/hexdump.h>
#include <lk/console_cmd.h>
#include <platform/bcm28xx/hvs.h>
#include <stdio.h>
#include <string.h>

typedef struct {
  void (*update)(uint32_t stat);
  int duration;
} animation_t;

const int channel = PRIMARY_HVS_CHANNEL;
extern int state;
extern const int state_count;
extern int substate;
extern const animation_t animations[];

hvs_layer sprite;
yuv_image_2plane *img;

void yuv_putuv(yuv_image_2plane *i, int posx, int posy, uint8_t u, uint8_t v) {
  int xoff = posx * 2;
  int yoff = posy * i->chroma_stride;
  i->chroma[yoff + xoff + 0] = u;
  i->chroma[yoff + xoff + 1] = v;
  //if ((posy < 2) && (posx < 10)) printf("%d,%d = %d %d\n", posx, posy, u, v);
}

void yuv_puty(yuv_image_2plane *i, int posx, int posy, uint8_t y) {
  i->luma[(posy * i->luma_stride) + posx] = y;
}

static int cmd_fill(int argc, const console_cmd_args *argv) {
  if (argc < 3) {
    puts("usage: yuv_fill <u> <v>\n");
    return 0;
  }
  uint8_t u = argv[1].u;
  uint8_t v = argv[2].u;
  for (int x=0; x<20; x++) {
    for (int y=0; y<20; y++) {
      yuv_putuv(img, x, y, u, v);
    }
  }
  return 0;
}

static int cmd_fill2(int argc, const console_cmd_args *argv) {
  if (argc < 3) {
    puts("usage: yuv_fill2 <x0> <x1>\n");
    return 0;
  }
  uint8_t x0 = argv[1].u;
  uint8_t x1 = argv[2].u;
  for (int x=x0; x<x1; x++) {
    for (int y=0; y<20; y++) {
      if (y == 0) printf("%d,%d == 0 %d %d\n", x, y, x*2, x*2);
      yuv_putuv(img, x, y, x*2, x*2);
    }
  }
  return 0;
}

static int cmd_filly(int argc, const console_cmd_args *argv) {
  if (argc < 4) {
    puts("usage: yuv_filly <x0> <x1> <luma>\n");
    return 0;
  }
  uint8_t x0 = argv[1].u;
  uint8_t x1 = argv[2].u;
  uint8_t luma = argv[3].u;
  for (int x=x0; x<x1; x++) {
    for (int y=0; y<256; y++) {
      img->luma[(y * img->luma_stride) + x] = luma;
    }
  }
  return 0;
}

static int cmd_yuv_peek(int argc, const console_cmd_args *argv) {
  if (argc < 3) {
    puts("usage: yuv_peek <x> <y>\n");
    return 0;
  }
  int x = argv[1].u;
  int y = argv[2].u;
  uint8_t luma = img->luma[(y * img->luma_stride)+x];
  uint8_t u = img->chroma[((y / 2) * img->chroma_stride) + (x/2)];
  uint8_t v = img->chroma[((y / 2) * img->chroma_stride) + (x/2) + 1];
  printf("%d,%d == %d %d %d\n", x, y, luma, u, v);
  return 0;
}

STATIC_COMMAND_START
STATIC_COMMAND("yuv_fill", "", &cmd_fill)
STATIC_COMMAND("yuv_fill2", "", &cmd_fill2)
STATIC_COMMAND("yuv_filly", "", &cmd_filly)
STATIC_COMMAND("yuv_peek", "", &cmd_yuv_peek)
STATIC_COMMAND_END(yuv);

bool state_advance(void) {
  substate++;
  if (substate > animations[state].duration) {
    substate = 0;
    state++;
    state = state % state_count;
    return true;
  }
  return false;
}

void animate_x(uint32_t stat) {
  sprite.x = (substate * 2) & 0x1ff;
  if (state_advance()) sprite.x = 116;
}

void animate_y(uint32_t stat) {
  sprite.y = (substate * 2) & 0x1ff;
  if (state_advance()) sprite.y = 116;
}

void animate_alpha(uint32_t stat) {
  sprite.alpha_mode = alpha_mode_fixed_nonzero;
  sprite.alpha = 255 - (substate & 0xff);
  if (state_advance()) {
    sprite.alpha_mode = alpha_mode_fixed;
    sprite.alpha = 0xff;
  }
}

#if 0
void animate_chroma_hscale(uint32_t stat) {
  img->chroma_hscale = (substate/10)+1;
  if (state_advance()) {
    img->chroma_hscale = 200;
  }
}
#endif

int state = 0;
int substate = 0;

const animation_t animations[] = {
  { animate_x, 256 },
  { animate_y, 256 },
  { animate_alpha, 256 },
  //{ animate_chroma_hscale, 2000 },
};
const int state_count = sizeof(animations) / sizeof(animations[0]);

#define RED   0xffff0000
#define GREEN 0xff00ff00
#define BLUE  0xff0000ff
#define OPAQUE 0xff000000

static void draw_grid(void) {
  int grid = 16;
  int width = grid * 18;
  int height = grid * 18;
  gfx_surface *gfx_grid = gfx_create_surface(NULL, width, height, width, GFX_FORMAT_ARGB_8888);
  hvs_layer *grid_layer = malloc(sizeof(hvs_layer));

  for (int x=0; x< width; x++) {
    for (int y=0; y < height; y++) {
      uint color = 0x00000000;
      if (true) {
        if ((y % grid == 0) || (y % grid == 1)) {
          color |= RED;
        }
        if ((x % grid == 0) || (x % grid == 1)) {
          color |= GREEN;
        }
      }
      gfx_putpixel(gfx_grid, x, y, color);
    }
  }
  mk_unity_layer(grid_layer, gfx_grid, 60, 100, 100);
  hvs_allocate_premade(grid_layer, 7);
  hvs_regen_noscale_noviewport_noalpha(grid_layer);
  grid_layer->name = strdup("grid");
  mutex_acquire(&channels[channel].lock);
  hvs_dlist_add(channel, grid_layer);
  hvs_update_dlist(channel);
  mutex_release(&channels[channel].lock);
}

void hvs_dlist_update_yuv(hvs_layer *s, yuv_image_2plane *i) {
  // HVS_PIXEL_FORMAT_YCBCR_YUV420_2PLANE:
  // the hardware is aware that the luma plane is the size specified in POS2, and the chroma plane is half of POS2
  // but the scale parameters for luma and chroma are user supplied, and can violate those assumptions
  // when doing a chroma lookup, it will follow the user-specified chroma scale, but then clamp to half of POS2
  // if the scale parameters lead to a lookup beyond the assumed chroma size, it will repeat whatever is at the edge of the chroma image
  int fmt = HVS_PIXEL_FORMAT_YCBCR_YUV420_2PLANE;

  // UV scale
  uint32_t scl0 = SCALER_CTL0_SCL_H_PPF_V_PPF;
  // Y scale
  uint32_t scl1 = SCALER_CTL0_SCL_H_PPF_V_PPF;

  s->dlist_length = 25;
  uint32_t *d = s->premade_dlist;
  // CTL0
  d[0] = CONTROL_VALID
    | CONTROL_PIXEL_ORDER(HVS_PIXEL_ORDER_XYCBCR)
    | CONTROL_FORMAT(fmt)
    | CONTROL_SCL0(scl0)
    | CONTROL_SCL1(scl1)
    | CONTROL_WORDS(s->dlist_length);
  // POS0
  d[1] = POS0_X(s->x) | POS0_Y(s->y) | POS0_ALPHA(s->alpha);
  // POS1 optional scaled output size
  d[2] = s->w | (s->h << 16);
  // POS2, input size
  d[3] = POS2_H(s->orig_h) | POS2_W(s->orig_w) | (s->alpha_mode << 30); // fixed alpha
  // POS3, context
  d[4] = 0xDEADBEEF;
  // PTR0
  d[5] = (uint32_t)i->luma | 0xc0000000;
  // PTR1
  d[6] = (uint32_t)i->chroma | 0xc0000000;
  // context 0
  d[7] = 0xDEADBEEF;
  // context 1
  d[8] = 0xDEADBEEF;
  // pitch 0
  d[9] = i->luma_stride;
  // pitch 1
  d[10] = i->chroma_stride;
  // colorspace conversion
  d[11] = 0x00f00000;
  d[12] = 0xe73304a8;
  d[13] = 0x00066604;
  // LMB base addr
  d[14] = 600;
  // scaling parameters UV
  //d[14] = gen_ppf(100, 100); // ???
  d[15] = gen_ppf(100, 200); // chroma H scale
  d[16] = gen_ppf(100, 200); // chroma V scale
  d[17] = 0xDEADBEEF;
  // scaling parameters Y
  d[18] = gen_ppf(256, 256);
  d[19] = gen_ppf(256, 256);
  d[20] = 0xDEADBEEF;

  // scaling kernels
  d[21] = scaling_kernel;
  d[22] = scaling_kernel;
  d[23] = scaling_kernel;
  d[24] = scaling_kernel;
}

yuv_image_2plane *yuv_allocate(unsigned int width, unsigned int height, int chroma_hscale, int chroma_vscale) {
  yuv_image_2plane *i = malloc(sizeof(yuv_image_2plane));
  i->luma_stride = width;
  i->chroma_stride = (width / chroma_hscale) * 2;
  i->luma = malloc(height * i->luma_stride);
  i->chroma = malloc((height / chroma_vscale) * i->chroma_stride);
  i->chroma_hscale = chroma_hscale * 100;
  i->chroma_vscale = chroma_vscale * 100;
  return i;
}

yuv_image_2plane *yuv420_allocate(unsigned int width, unsigned int height) {
  return yuv_allocate(width, height, 2, 2);
}

void create_yuv_sweep(void) {
  img = yuv420_allocate(256,256);

  sprite.fb = NULL;
  sprite.layer = 60;
  sprite.x = 116;
  sprite.y = 116;
  sprite.w = 256;
  sprite.h = 256;

  sprite.orig_w = 256;
  sprite.orig_h = 256;

  sprite.viewport_w = 256;
  sprite.viewport_h = 256;
  sprite.viewport_x = 0;
  sprite.viewport_y = 0;

  sprite.visible = true;

  sprite.palette_mode = palette_none;

  sprite.premade_dlist = malloc(4*32);

  sprite.alpha_mode = alpha_mode_fixed;
  sprite.alpha = 0xff;

  sprite.name = strdup("YUV");

  for (int y=0; y<128; y++) {
    for (int x=0; x<(128); x++) {
      yuv_putuv(img, x, y, x*2, 254 - (y*2));
      //yuv_putpixel(x, y, 0, 128, 128);
    }
  }
  for (int y=0; y<256; y++) {
    for (int x=0; x<256; x++) {
      yuv_puty(img, x, y, 128);
    }
  }
}

void fillrect(yuv_image_2plane *i, int x0, int y0, int w, int h, uint8_t luma, uint8_t u, uint8_t v) {
  for (int y=y0/2; y<((y0/2)+(h/2)); y++) {
    for (int x=x0/2; x<((x0/2)+(w/2)); x++) {
      yuv_putuv(i, x, y, u, v);
    }
  }
  for (int y=y0; y < (y0+h); y++) {
    for (int x=x0; x < (x0+w); x++) {
      yuv_puty(i, x, y, luma);
    }
  }
}

void create_yuv_colorbars(void) {
  int w = 620;
  int h = 400;
  img = yuv420_allocate(w, h);

  sprite.fb = NULL;
  sprite.layer = 60;
  sprite.x = 50;
  sprite.y = 30;
  sprite.w = w;
  sprite.h = h;

  sprite.orig_w = w;
  sprite.orig_h = h;

  sprite.viewport_w = 256;
  sprite.viewport_h = 256;
  sprite.viewport_x = 0;
  sprite.viewport_y = 0;

  sprite.visible = true;

  sprite.palette_mode = palette_none;

  sprite.premade_dlist = malloc(4*32);

  sprite.alpha_mode = alpha_mode_fixed;
  sprite.alpha = 0xff;

  sprite.name = strdup("YUV");

  memset(img->chroma, 128, h * img->chroma_stride);
  memset(img->luma, 0, h * img->luma_stride);

  int slice = w/7;
  fillrect(img, slice*0, 0, slice, h, 180, 128, 128);
  fillrect(img, slice*1, 0, slice, h, 168, 44, 136);
  fillrect(img, slice*2, 0, slice, h, 145, 147, 44);
  fillrect(img, slice*3, 0, slice, h, 133, 63, 52);
  fillrect(img, slice*4, 0, slice, h, 63, 193, 204);
  fillrect(img, slice*5, 0, slice, h, 51, 109, 212);
  fillrect(img, slice*6, 0, slice, h, 28, 212, 120);
}

static void yuv_entry(const struct app_descriptor *app, void *args) {
  draw_grid();

  //create_yuv_colorbars();
  create_yuv_sweep();

  hvs_dlist_update_yuv(&sprite, img);

  mutex_acquire(&channels[channel].lock);
  hvs_dlist_add(channel, &sprite);
  hvs_update_dlist(channel);
  mutex_release(&channels[channel].lock);


  while (true) {
    uint32_t stat = hvs_wait_vsync(channel);
    animations[state].update(stat);

    hvs_dlist_update_yuv(&sprite, img);

    mutex_acquire(&channels[channel].lock);
    hvs_update_dlist(channel);
    mutex_release(&channels[channel].lock);
  }
}

APP_START(yuv)
  .entry = yuv_entry,
APP_END
