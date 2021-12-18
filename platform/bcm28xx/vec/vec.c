#include <app.h>
#include <lk/console_cmd.h>
#include <lk/init.h>
#include <lk/reg.h>
#include <platform/bcm28xx/clock.h>
#include <platform/bcm28xx/cm.h>
#include <platform/bcm28xx/hvs.h>
#include <platform/bcm28xx/pll.h>
#include <platform/bcm28xx/pll_read.h>
#include <platform/bcm28xx/power.h>
#include <platform/bcm28xx/pv.h>
#include <platform/bcm28xx/vec.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef WITH_TGA
#include <lib/tga.h>
#include "pi-logo.h"
#include "ResD1_720X480.h"
//#include <dance.h>
#endif

//extern uint8_t* pilogo;

enum vec_mode {
  ntsc,
  ntscj,
  pal,
  palm,
};


#ifdef WITH_TGA
static gfx_surface *logo;
#endif


static void draw_background_grid(void) {
  //hvs_add_plane(gfx_grid, 0, 0, false);
}

static void vec_init(uint level) {
  int channel = 1; // on VC4, the VEC is hard-wired to hvs channel 1
  int width;
  int height;

  puts("VEC init");

#if !defined(RPI4)
  power_up_usb();
#endif
  hvs_initialize();

  // TODO, this assumes freq_pllc_per is a multiple of 108mhz
  // it should check if it isnt, and report an error
  int desired_divider = freq_pllc_per / 108000000;

  *REG32(CM_VECDIV) = CM_PASSWORD | desired_divider << 12;
  *REG32(CM_VECCTL) = CM_PASSWORD | CM_SRC_PLLC_CORE0; // technically its on the PER tap
  *REG32(CM_VECCTL) = CM_PASSWORD | CM_VECCTL_ENAB_SET | CM_SRC_PLLC_CORE0;
  int rate = measure_clock(29);
  printf("vec rate: %f\n", ((double)rate)/1000/1000);

  *REG32(VEC_WSE_RESET) = 1;
  *REG32(VEC_SOFT_RESET) = 1;
  *REG32(VEC_WSE_CONTROL) = 0;

  *REG32(VEC_SCHPH) = 0x28;
  *REG32(VEC_CLMP0_START) = 0xac;
  *REG32(VEC_CLMP0_END) = 0xac;
  *REG32(VEC_CONFIG2) = VEC_CONFIG2_UV_DIG_DIS | VEC_CONFIG2_RGB_DIG_DIS;
  *REG32(VEC_CONFIG3) = VEC_CONFIG3_HORIZ_LEN_STD;
#ifdef RPI4
  *REG32(VEC_DAC_CONFIG) = VEC_DAC_CONFIG_DAC_CTRL(0x0) | VEC_DAC_CONFIG_DRIVER_CTRL(0x80) | VEC_DAC_CONFIG_LDO_BIAS_CTRL(0x61);
#else
  *REG32(VEC_DAC_CONFIG) = VEC_DAC_CONFIG_DAC_CTRL(0xc) | VEC_DAC_CONFIG_DRIVER_CTRL(0xc) | VEC_DAC_CONFIG_LDO_BIAS_CTRL(0x46);
#endif
  *REG32(VEC_MASK0) = 0;
  enum vec_mode mode = ntsc;
  switch (mode) {
    case ntsc:
      *REG32(VEC_CONFIG0) = VEC_CONFIG0_NTSC_STD | VEC_CONFIG0_PDEN;
      *REG32(VEC_CONFIG1) = VEC_CONFIG1_C_CVBS_CVBS;
      break;
    case ntscj:
      *REG32(VEC_CONFIG0) = VEC_CONFIG0_NTSC_STD;
      *REG32(VEC_CONFIG1) = VEC_CONFIG1_C_CVBS_CVBS;
      break;
    case pal:
      *REG32(VEC_CONFIG0) = VEC_CONFIG0_PAL_BDGHI_STD;
      *REG32(VEC_CONFIG1) = VEC_CONFIG1_C_CVBS_CVBS;
      break;
    case palm:
      *REG32(VEC_CONFIG0) = VEC_CONFIG0_PAL_BDGHI_STD;
      *REG32(VEC_CONFIG1) = VEC_CONFIG1_C_CVBS_CVBS | VEC_CONFIG1_CUSTOM_FREQ;
      *REG32(VEC_FREQ3_2) = 0x223b;
      *REG32(VEC_FREQ1_0) = 0x61d1;
      break;
  }
  *REG32(VEC_DAC_MISC) = VEC_DAC_MISC_VID_ACT | VEC_DAC_MISC_DAC_RST_N;
  *REG32(VEC_CFG) = VEC_CFG_VEC_EN;
  struct pv_timings t;
  t.clock_mux = clk_vec;
  bool ntsc_mode = true;
  if (ntsc_mode) {
    t.vfp = 3;
    t.vsync = 4; // try 3
    t.vbp = 16;
    t.vactive = 240;

    t.vfp_even = 4; // try 3
    t.vsync_even = 3; // try 4
    t.vbp_even = 16;
    t.vactive_even = 240;

    t.interlaced = true;

    t.hfp = 14;
    t.hsync = 64;
    t.hbp = 60;
    t.hactive = 720;
  }
#ifdef RPI4
  setup_pixelvalve(&t, 3);
#else
  setup_pixelvalve(&t, 2);
#endif

  uint32_t t0 = *REG32(ST_CLO);
  printf("NTSC on at %d\n", t0);

  width = t.hactive;
  height = t.vactive * 2;

  mutex_acquire(&channels[channel].lock);
  hvs_update_dlist(channel);
  mutex_release(&channels[channel].lock);

  hvs_configure_channel(1, width, height, true);
  hvs_set_background_color(channel, 0x0);

#ifdef WITH_TGA
  //logo = tga_decode(pilogo, sizeof(pilogo), GFX_FORMAT_ARGB_8888);

  if (false) {
    hvs_layer *new_layer = malloc(sizeof(hvs_layer));
    new_layer->layer = 100;
    new_layer->fb = logo;
    new_layer->x = 50;
    new_layer->y = 50;
    new_layer->w = logo->width / 4;
    new_layer->h = logo->height / 4;
    new_layer->name = strdup("logo 1");
    hvs_dlist_add(channel, new_layer);
  }

  //dance_start(logo, 1);
#endif
}

//static void vec_entry(const struct app_descriptor *app, void *args) {
//}

LK_INIT_HOOK(vec, &vec_init, LK_INIT_LEVEL_PLATFORM - 1);
