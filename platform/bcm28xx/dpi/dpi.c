#include <dev/gpio.h>
#include <kernel/timer.h>
#include <lk/console_cmd.h>
#include <lk/init.h>
#include <lk/reg.h>
#include <platform/bcm28xx/cm.h>
#include <platform/bcm28xx/dpi.h>
#include <platform/bcm28xx/gpio.h>
#include <platform/bcm28xx/hvs.h>
#include <platform/bcm28xx/pll_read.h>
#include <platform/bcm28xx/power.h>
#include <platform/bcm28xx/pv.h>
#include <stdio.h>
#include <stdlib.h>

int cmd_dpi_start(int argc, const console_cmd_args *argv);

STATIC_COMMAND_START
STATIC_COMMAND("dpi_start", "start DPI interface", &cmd_dpi_start)
STATIC_COMMAND_END(dpi);

//#define HYPERPIXEL
#define GERTVGA
#define AVOID_MASH true

#ifndef BACKGROUND
#define BACKGROUND 0x0
#endif

static void timings_vga(struct pv_timings *t, int *fps) {
  t->vfp = 3;
  t->vsync = 4;
  t->vbp = 13;
  t->vactive = 480;

  t->hfp = 60;
  t->hsync = 64;
  t->hbp = 80;
  t->hactive = 640;
  *fps = 60;
}

static const struct pv_timings timing_1280_1024 = {
  .vfp = 3,
  .vsync = 7,
  .vbp = 29,
  .vactive = 1024,

  .hfp = 80,
  .hsync = 136,
  .hbp = 216,
  .hactive = 1280,

  .clock_mux = clk_dpi_smi_hdmi,
  .interlaced = false
};

static const struct pv_timings * timings_1280_1024(int *fps) {
  return &timing_1280_1024;
  *fps = 60;
}

int cmd_dpi_start(int argc, const console_cmd_args *argv) {
  power_up_image();
  hvs_initialize();

  const struct pv_timings *t;
  int fps = 60;

#ifdef HYPERPIXEL
  t.vfp = 15;
  t.vsync = 113;
  t.vbp = 15;
  t.vactive = 800;

  t.hfp = 10;
  t.hsync = 16;
  t.hbp = 59;
  t.hactive = 480;
#elif defined(GERTVGA)
  //timings_vga(&t, &fps);
  t = timings_1280_1024(&fps);
#else
  t.vfp = 0;
  t.vsync = 1;
  t.vbp = 0;
  t.vactive = 10;

  t.hfp = 0;
  t.hsync = 1;
  t.hbp = 0;
  t.hactive = 10;
#endif

  hvs_configure_channel(0, t->hactive, t->vactive, false);

  int htotal = t->hfp + t->hsync + t->hbp + t->hactive;
  int vtotal = t->vfp + t->vsync + t->vbp + t->vactive;
  int total_pixels = htotal * vtotal;

  float desired_divider = (float)freq_pllc_per / total_pixels / fps;

#if 1
  int lower_fps = freq_pllc_per / (int)desired_divider / total_pixels;
  int higher_fps = freq_pllc_per / ((int)desired_divider+1) / total_pixels;
  printf("divisor %f, fps bounds %d-%d, ", (double)desired_divider, lower_fps, higher_fps);
#endif
  if (AVOID_MASH) {
    desired_divider = (int)(desired_divider + 0.5f);
    printf("AVOID_MASH set, divider forced to %d, ", (int)desired_divider);
  }

#ifdef HYPERPIXEL
  *REG32(CM_DPIDIV) = CM_PASSWORD | (0xe00 << 4);
  *REG32(CM_DPICTL) = CM_PASSWORD | CM_DPICTL_KILL_SET | CM_SRC_PLLC_CORE0;
  while (*REG32(CM_DPICTL) & CM_DPICTL_BUSY_SET) {};
  *REG32(CM_DPICTL) = CM_PASSWORD | CM_DPICTL_ENAB_SET | CM_SRC_PLLC_CORE0;
  while (*REG32(CM_DPICTL) & CM_DPICTL_BUSY_SET) {};
#elif defined(GERTVGA)
  int fixed_point_divider = desired_divider * 0x100;
  *REG32(CM_DPIDIV) = CM_PASSWORD | (fixed_point_divider << 4);
  *REG32(CM_DPICTL) = CM_PASSWORD | CM_DPICTL_KILL_SET | CM_SRC_PLLC_CORE0;
  while (*REG32(CM_DPICTL) & CM_DPICTL_BUSY_SET) {};
  *REG32(CM_DPICTL) = CM_PASSWORD | CM_DPICTL_ENAB_SET | CM_SRC_PLLC_CORE0;
  while (*REG32(CM_DPICTL) & CM_DPICTL_BUSY_SET) {};
#else
  *REG32(CM_DPIDIV) = CM_PASSWORD | (0xf00 << 4);
  *REG32(CM_DPICTL) = CM_PASSWORD | CM_DPICTL_KILL_SET | CM_SRC_OSC;
  while (*REG32(CM_DPICTL) & CM_DPICTL_BUSY_SET) {};
  *REG32(CM_DPICTL) = CM_PASSWORD | CM_DPICTL_ENAB_SET | CM_SRC_OSC;
  while (*REG32(CM_DPICTL) & CM_DPICTL_BUSY_SET) {};
#endif
  int rate = measure_clock(17);
  printf("DPI clock measured at %d KHz, ", rate/1000);

  printf("hsync rate: %d Hz, ", rate / htotal);
  printf("vsync rate: %d Hz, ", rate / total_pixels);
  printf("htotal: %d, vtotal: %d\n", htotal, vtotal);

  setup_pixelvalve(t, 0);

  int dpi_output_format;
#ifdef HYPERPIXEL
  dpi_output_format = 0x7f226;
#elif defined(GERTVGA)
  dpi_output_format = 0x45;
#else
  dpi_output_format = 0x6;
#endif
  int format = (dpi_output_format & 0xf) - 1;
  int rgb_order = (dpi_output_format >> 4) & 0xf;

  int output_enable_mode    = (dpi_output_format >> 8) & 0x1;
  int invert_pixel_clock    = (dpi_output_format >> 9) & 0x1;

  int hsync_disable         = (dpi_output_format >> 12) & 0x1;
  int vsync_disable         = (dpi_output_format >> 13) & 0x1;
  int output_enable_disable = (dpi_output_format >> 14) & 0x1;

  int hsync_polarity        = (dpi_output_format >> 16) & 0x1;
  int vsync_polarity        = (dpi_output_format >> 17) & 0x1;
  int output_enable_polarity = (dpi_output_format >> 18) & 0x1;

  int hsync_phase           = (dpi_output_format >> 20) & 0x1;
  int vsync_phase           = (dpi_output_format >> 21) & 0x1;
  int output_enable_phase   = (dpi_output_format >> 22) & 0x1;

  uint32_t control_word = DPI_ENABLE;

  printf("format: %d\n", format);
  control_word |= FORMAT(format);

  printf("rgb order: %d\n", rgb_order);
  control_word |= ORDER(rgb_order);

  if (output_enable_mode) {
    puts("output enable mode");
    control_word |= DPI_OUTPUT_ENABLE_MODE;
  }
  if (invert_pixel_clock) {
    puts("invert pixel clock");
    control_word |= DPI_PIXEL_CLK_INVERT;
  }
  if (hsync_disable) {
    puts("hsync disable");
    control_word |= DPI_HSYNC_DISABLE;
  }
  if (vsync_disable) {
    puts("vsync disable");
    control_word |= DPI_VSYNC_DISABLE;
  }
  if (output_enable_disable) {
    puts("output_enable_disable");
  }
  if (hsync_polarity) {
    puts("hsync polarity");
  }
  if (vsync_polarity) {
    puts("vsync polarity");
  }
  if (output_enable_polarity) {
    puts("output_enable_polarity");
  }
  if (hsync_phase) {
    puts("hsync_phase");
  }
  if (vsync_phase) {
    puts("vsync_phase");
  }
  if (output_enable_phase) {
    puts("output_enable_phase");
  }


  *REG32(DPI_C) = control_word;
#ifdef HYPERPIXEL
  for (int x=0; x<26; x++) {
    if (x == 10) {}
    else if (x == 11) {}
    else if (x == 14) {}
    else if (x == 15) {}
    else if (x == 18) {}
    else if (x == 19) {}
    else gpio_config(x, kBCM2708Pinmux_ALT2);
  }
#elif defined(AUTO)
  for (int x=0; x<28; x++) {
    if ((x == 2) && vsync_disable) {}
    else if ((x == 3) && hsync_disable) {}
    else if (x == 14) {}
    else gpio_config(x, kBCM2708Pinmux_ALT2);
  }
#else
  gpio_config(0, kBCM2708Pinmux_ALT2); // pixel-clock
  gpio_config(2, kBCM2708Pinmux_ALT2); // vsync
  gpio_config(3, kBCM2708Pinmux_ALT2); // hsync
  gpio_config(4, kBCM2708Pinmux_ALT2); // D0
  gpio_config(5, kBCM2708Pinmux_ALT2); // D1

  for (int i=4; i<=9; i++) {
    gpio_config(i, kBCM2708Pinmux_ALT2);
  }
  for (int i=10; i<=15; i++) {
    //if (i == 14) continue;
    //if (i == 15) continue;
    gpio_config(i, kBCM2708Pinmux_ALT2);
  }
  for (int i=16; i<=21; i++) {
    gpio_config(i, kBCM2708Pinmux_ALT2);
  }
  hvs_set_background_color(0, BACKGROUND);
#endif
  return 0;
}

static void dpi_init(uint level) {
  cmd_dpi_start(0, NULL);
}

LK_INIT_HOOK(dpi, &dpi_init, LK_INIT_LEVEL_PLATFORM - 1);
