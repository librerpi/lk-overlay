#pragma once

#include <kernel/mutex.h>
#include <lib/gfx.h>
#include <lk/console_cmd.h>
#include <lk/list.h>
#include <platform/bcm28xx.h>
#include <stdlib.h>

#define SCALER_BASE (BCM_PERIPH_BASE_VIRT + 0x400000)

#define SCALER_DISPCTRL     (SCALER_BASE + 0x00)
#define SCALER_DISPSTAT     (SCALER_BASE + 0x04)
#define SCALER_DISPCTRL_ENABLE  (1<<31)
#define SCALER_DISPECTRL    (SCALER_BASE + 0x0c)
#define SCALER_DISPECTRL_SECURE_MODE    (1<<31)
#define SCALER_DISPDITHER   (SCALER_BASE + 0x14)
#define SCALER_DISPEOLN     (SCALER_BASE + 0x18)
#define SCALER_DISPLIST0    (SCALER_BASE + 0x20)
#define SCALER_DISPLIST1    (SCALER_BASE + 0x24)
#define SCALER_DISPLIST2    (SCALER_BASE + 0x28)

struct hvs_channel {
  volatile uint32_t dispctrl;
  volatile uint32_t dispbkgnd;
  volatile uint32_t dispstat;
  // 31:30  mode
  // 29     full
  // 28     empty
  // 17:12  frame count
  // 11:0   line
  volatile uint32_t dispbase;
};

extern volatile struct hvs_channel *hvs_channels;

struct hvs_channel_config {
  uint32_t width;
  uint32_t height;
  bool interlaced;

  mutex_t lock;
  struct list_node layers;
  uint32_t dlist_target;
  wait_queue_t vsync;
};

extern struct hvs_channel_config channels[3];

enum hvs_pixel_format {
	/* 8bpp */
	HVS_PIXEL_FORMAT_RGB332 = 0,
	/* 16bpp */
	HVS_PIXEL_FORMAT_RGBA4444 = 1,
	HVS_PIXEL_FORMAT_RGB555 = 2,
	HVS_PIXEL_FORMAT_RGBA5551 = 3,
	HVS_PIXEL_FORMAT_RGB565 = 4,
	/* 24bpp */
	HVS_PIXEL_FORMAT_RGB888 = 5,
	HVS_PIXEL_FORMAT_RGBA6666 = 6,
	/* 32bpp */
	HVS_PIXEL_FORMAT_RGBA8888 = 7,

	HVS_PIXEL_FORMAT_YCBCR_YUV420_3PLANE = 8,
	HVS_PIXEL_FORMAT_YCBCR_YUV420_2PLANE = 9,
	HVS_PIXEL_FORMAT_YCBCR_YUV422_3PLANE = 10,
	HVS_PIXEL_FORMAT_YCBCR_YUV422_2PLANE = 11,
	HVS_PIXEL_FORMAT_H264 = 12,
	HVS_PIXEL_FORMAT_PALETTE = 13,
	HVS_PIXEL_FORMAT_YUV444_RGB = 14,
	HVS_PIXEL_FORMAT_AYUV444_RGB = 15,
	HVS_PIXEL_FORMAT_RGBA1010102 = 16,
	HVS_PIXEL_FORMAT_YCBCR_10BIT = 17,
};

enum palette_type {
  palette_none = 0,
  palette_1bpp = 1,
  palette_2bpp = 2,
  palette_4bpp = 3,
  palette_8bpp = 4,
};

enum alpha_mode {
  alpha_mode_pipeline = 0,      // per-pixel alpha allowed, POS0_ALPHA ignored
  alpha_mode_fixed = 1,         // use POS0_ALPHA() for entire sprite
  alpha_mode_fixed_nonzero = 2, // POS0_ALPHA() and per-pixel both have an effect
  alpha_mode_fixed_over_7 = 3,
};

typedef struct {
  uint32_t table[256];
  enum palette_type type;
} palette_table;

typedef struct {
  uint8_t *luma;
  uint8_t *chroma;
  unsigned int luma_stride;
  unsigned int chroma_stride;
  unsigned int chroma_hscale;
  unsigned int chroma_vscale;
} yuv_image_2plane;

typedef struct {
  struct list_node node;
  gfx_surface *fb;

  // screen XY to render at
  int x;
  int y;

  unsigned int orig_w;
  unsigned int orig_h;

  // the final WH on the screen
  unsigned int w;
  unsigned int h;

  // the priority, when multiple layers overlap
  int layer;

  // a crop applied to the input image
  unsigned int viewport_x, viewport_y, viewport_w, viewport_h;

  char *name;
  bool visible;
  void *rawImage;
  enum palette_type palette_mode;
  uint strides[2];
  const palette_table *colors;

  uint32_t *premade_dlist;
  uint32_t dlist_length;
  enum alpha_mode alpha_mode;
  uint8_t alpha;
} hvs_layer;

typedef struct {
  int x;
  int y;
  int width;
  int height;
} framebuffer_pos;

//#define MK_UNITY_LAYER(l, FB, LAYER, X, Y) { (l)->fb = FB; (l)->x = X; (l)->y = Y; (l)->layer = LAYER; (l)->w = FB->width; (l)->h = FB->height; }

#define SCALER_STAT_LINE(n) ((n) & 0xfff)

#define SCALER_DISPCTRL0    (SCALER_BASE + 0x40)
#define SCALER_DISPCTRLX_ENABLE (1<<31)
#define SCALER_DISPCTRLX_RESET  (1<<30)
#define SCALER_DISPCTRL_W(n)    ((n & 0xfff) << 12)
#define SCALER_DISPCTRL_H(n)    (n & 0xfff)
#define SCALER_DISPBKGND_AUTOHS    (1<<31)
#define SCALER_DISPBKGND_INTERLACE (1<<30)
#define SCALER_DISPBKGND_GAMMA     (1<<29)
#define SCALER_DISPBKGND_FILL      (1<<24)

#define BASE_BASE(n) (n & 0xffff)
#define BASE_TOP(n) ((n & 0xffff) << 16)


#define SCALER_LIST_MEMORY  (SCALER_BASE + 0x2000)
#define SCALER5_LIST_MEMORY  (SCALER_BASE + 0x4000)


#define CONTROL_FORMAT(n)       (n & 0xf)
#define CONTROL_END             (1<<31)
#define CONTROL_VALID           (1<<30)
#define CONTROL_WORDS(n)        (((n) & 0x3f) << 24)
#define CONTROL0_FIXED_ALPHA    (1<<19)
#define CONTROL0_HFLIP          (1<<16)
#define CONTROL0_VFLIP          (1<<15)
#define CONTROL_PIXEL_ORDER(n)  ((n & 3) << 13)
#define CONTROL_SCL1(scl)       (scl << 8)
#define CONTROL_SCL0(scl)       (scl << 5)
#define CONTROL_UNITY           (1<<4)

#define HVS_PIXEL_ORDER_RGBA			0
#define HVS_PIXEL_ORDER_BGRA			1
#define HVS_PIXEL_ORDER_ARGB			2
#define HVS_PIXEL_ORDER_ABGR			3

#define HVS_PIXEL_ORDER_XBRG			0
#define HVS_PIXEL_ORDER_XRBG			1
#define HVS_PIXEL_ORDER_XRGB			2
#define HVS_PIXEL_ORDER_XBGR			3

#define HVS_PIXEL_ORDER_XYCBCR			0
#define HVS_PIXEL_ORDER_XYCRCB			1
#define HVS_PIXEL_ORDER_YXCBCR			2
#define HVS_PIXEL_ORDER_YXCRCB			3

#define SCALER_CTL0_SCL_H_PPF_V_PPF		0
#define SCALER_CTL0_SCL_H_TPZ_V_PPF		1
#define SCALER_CTL0_SCL_H_PPF_V_TPZ		2
#define SCALER_CTL0_SCL_H_TPZ_V_TPZ		3
#define SCALER_CTL0_SCL_H_PPF_V_NONE		4
#define SCALER_CTL0_SCL_H_NONE_V_PPF		5
#define SCALER_CTL0_SCL_H_NONE_V_TPZ		6
#define SCALER_CTL0_SCL_H_TPZ_V_NONE		7

#define POS0_X(n) (n & 0xfff)
#define POS0_Y(n) ((n & 0xfff) << 12)
#define POS0_ALPHA(n) ((n & 0xff) << 24)

#define POS2_W(n) (n & 0xffff)
#define POS2_H(n) ((n & 0xffff) << 16)

#define SCALER_PPF_AGC (1<<30)

extern int display_slot;
extern volatile uint32_t* dlist_memory;
extern const int scaling_kernel;

//void hvs_add_plane(gfx_surface *fb, int x, int y, bool hflip);
//void hvs_add_plane_scaled(gfx_surface *fb, int x, int y, unsigned int width, unsigned int height, bool hflip);
//void hvs_terminate_list(void);
void hvs_wipe_displaylist(void);
void hvs_initialize(void);
void hvs_configure_channel(int channel, int width, int height, bool interlaced);
void hvs_setup_irq(void);
// schedules a pageflip at the next vsync, based on the current state of the layers list
// can be called multiple times per frame
// if called multiple times, only the last state will be applied
// must be called with channel lock held
void hvs_update_dlist(int channel);
// adds an hvs_layer to the layer list
// do not change the ->layer while an hvs_layer is added
// x/y/w/h can be changed, and will take effect next time hvs_update_dlist is ran
void hvs_dlist_add(int channel, hvs_layer *new_layer);
// blocks the current thread until after a vsync has occured
// thread resumes too late for any page-flip actions
// but then you have an entire frametime to schedule a pageflip
// flip will happen at the NEXT vsync
// returns the value of the SCALER_DISPSTATn register, which holds the current frame and scanline#
uint32_t hvs_wait_vsync(int channel);
int cmd_hvs_dump_dlist(int argc, const console_cmd_args *argv);
// returns the recommended xywh of the framebuffer
void hvs_get_framebuffer_pos(int channel, framebuffer_pos *pos);

// functions to populate l->premade_dlist with the required values, reducing cpu usage for static sprites
// hvs_update_dlist() will read from the buffer, and could tear, it is recommended to hold the channels[channel].lock when updating any layer that is visible
// renders l->fb at 1:1 scale, no viewport cropping
void hvs_regen_noscale_noviewport(hvs_layer *l);
void hvs_regen_noscale_viewport_noalpha(hvs_layer *l);
void hvs_regen_scale_noviewport(hvs_layer *l);

inline uint32_t gen_ppf_fixedpoint(uint32_t source, uint32_t dest) {
  uint32_t scale = source / dest;
  return SCALER_PPF_AGC | (scale << 8) | (0 << 0);
}

inline uint32_t gen_ppf(unsigned int source, unsigned int dest) {
  return gen_ppf_fixedpoint(source << 16, dest);
}

// 0xRRGGBB
inline __attribute__((always_inline)) void hvs_set_background_color(int channel, uint32_t color) {
  hvs_channels[channel].dispbkgnd = SCALER_DISPBKGND_FILL | SCALER_DISPBKGND_AUTOHS | color
    | (channels[channel].interlaced ? SCALER_DISPBKGND_INTERLACE : 0);
}

static inline enum hvs_pixel_format gfx_to_hvs_pixel_format(gfx_format fmt) {
  switch (fmt) {
  case GFX_FORMAT_RGB_332:
    return HVS_PIXEL_FORMAT_RGB332; // 0
  case GFX_FORMAT_RGB_565:
    return HVS_PIXEL_FORMAT_RGB565; // 4
  case GFX_FORMAT_ARGB_8888:
  case GFX_FORMAT_RGB_x888:
    return HVS_PIXEL_FORMAT_RGBA8888; // 7
  default:
    printf("warning, unsupported pixel format: %d\n", fmt);
    return 0;
  }
}

static inline void mk_unity_layer(hvs_layer *l, gfx_surface *fb, int layer, unsigned int x, unsigned int y) {
  l->fb = fb;
  l->layer = layer;
  l->x = x;
  l->y = y;
  l->w = fb->width;
  l->h = fb->height;

  l->viewport_w = fb->width;
  l->viewport_h = fb->height;
  l->viewport_x = 0;
  l->viewport_y = 0;

  l->visible = true;

  l->palette_mode = palette_none;

  l->premade_dlist = NULL;
  l->dlist_length = 0;
}

static inline void hvs_allocate_premade(hvs_layer *l, int words) {
  l->dlist_length = words;
  l->premade_dlist = malloc(words * 4);
}

static inline uint palette_get_bpp(enum palette_type type) {
  switch (type) {
  case palette_none:
    return 0;
  case palette_1bpp:
    return 1;
  case palette_2bpp:
    return 2;
  case palette_4bpp:
    return 4;
  case palette_8bpp:
    return 8;
  }
  return 0;
}

static inline void mk_palette_layer(hvs_layer *l, int layer, unsigned int x, unsigned int y, uint width, uint height, enum palette_type type, const palette_table *colors) {
  l->fb = NULL;
  l->layer = layer;
  l->x = x;
  l->y = y;
  l->w = width;
  l->h = height;

  l->viewport_w = width;
  l->viewport_h = height;
  l->viewport_x = 0;
  l->viewport_y = 0;

  l->visible = true;

  l->palette_mode = type;
  l->strides[0] = ((width * palette_get_bpp(type)) + 7) / 8;
  l->rawImage = malloc(l->strides[0] * height);
  l->colors = colors;
}
