#pragma once

#include <kernel/mutex.h>
#include <lib/gfx.h>
#include <lk/console_cmd.h>
#include <lk/list.h>
#include <platform/bcm28xx.h>

#define SCALER_BASE (BCM_PERIPH_BASE_VIRT + 0x400000)

#define SCALER_DISPCTRL     (SCALER_BASE + 0x00)
#define SCALER_DISPSTAT     (SCALER_BASE + 0x04)
#define SCALER_DISPCTRL_ENABLE  (1<<31)
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

typedef struct {
  struct list_node node;
  gfx_surface *fb;
  int x;
  int y;
  int layer;
  int w;
  int h;
  const char *name;
} hvs_layer;

#define MK_UNITY_LAYER(l, FB, LAYER, X, Y) { (l)->fb = FB; (l)->x = X; (l)->y = Y; (l)->layer = LAYER; (l)->w = FB->width; (l)->h = FB->height; }

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
#define CONTROL_UNITY           (1<<4)

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

#define HVS_PIXEL_ORDER_RGBA			0
#define HVS_PIXEL_ORDER_BGRA			1
#define HVS_PIXEL_ORDER_ARGB			2
#define HVS_PIXEL_ORDER_ABGR			3

#define HVS_PIXEL_ORDER_XBRG			0
#define HVS_PIXEL_ORDER_XRBG			1
#define HVS_PIXEL_ORDER_XRGB			2
#define HVS_PIXEL_ORDER_XBGR			3

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

extern int display_slot;
extern volatile uint32_t* dlist_memory;

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

// 0xRRGGBB
inline __attribute__((always_inline)) void hvs_set_background_color(int channel, uint32_t color) {
  hvs_channels[channel].dispbkgnd = SCALER_DISPBKGND_FILL | SCALER_DISPBKGND_AUTOHS | color
    | (channels[channel].interlaced ? SCALER_DISPBKGND_INTERLACE : 0);
}
