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
int cmd_dpi_count(int argc, const console_cmd_args *argv);
int cmd_dpi_move(int argc, const console_cmd_args *argv);

STATIC_COMMAND_START
STATIC_COMMAND("dpi_start", "start DPI interface", &cmd_dpi_start)
STATIC_COMMAND("dpi_count", "begin counting on framebuffer", &cmd_dpi_count)
STATIC_COMMAND("dpi_move", "move a pixel on the frame", &cmd_dpi_move)
STATIC_COMMAND_END(dpi);

gfx_surface *framebuffer;
int width;
int height;
int stride;
timer_t updater;
uint8_t count;
uint32_t count2;

//#define HYPERPIXEL
#define GERTVGA

int cmd_dpi_start(int argc, const console_cmd_args *argv) {
  power_up_image();
  hvs_initialize();

  struct pv_timings t;
  t.clock_mux = clk_dpi_smi_hdmi;
  t.interlaced = false;
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
  t.vfp = 3;
  t.vsync = 4;
  t.vbp = 13;
  t.vactive = 480;

  t.hfp = 60;
  t.hsync = 64;
  t.hbp = 80;
  t.hactive = 640;
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

  if (!framebuffer) {
    width = t.hactive;
    height = t.vactive;
    stride = t.hactive;
    framebuffer = gfx_create_surface(NULL, width, height, width, GFX_FORMAT_ARGB_8888);
  }
  for (int x=0; x< height; x++) {
    for (int y=0; y < width; y++) {
      gfx_putpixel(framebuffer, x, y, (0xff<<24) | (y << 16) | (y << 8) | y);
    }
  }
  hvs_configure_channel(0, width, height, false);

#if 0
  int list_start = display_slot;
  hvs_add_plane(framebuffer, 0, 0, false);
  hvs_terminate_list();

  *REG32(SCALER_DISPLIST0) = list_start;
  *REG32(SCALER_DISPLIST1) = list_start;
  *REG32(SCALER_DISPLIST2) = list_start;
#endif


  // 0x200 means clock/2
#ifdef HYPERPIXEL
  *REG32(CM_DPIDIV) = CM_PASSWORD | (0xe00 << 4);
  *REG32(CM_DPICTL) = CM_PASSWORD | CM_DPICTL_KILL_SET | CM_SRC_PLLC_CORE0;
  while (*REG32(CM_DPICTL) & CM_DPICTL_BUSY_SET) {};
  *REG32(CM_DPICTL) = CM_PASSWORD | CM_DPICTL_ENAB_SET | CM_SRC_PLLC_CORE0;
  while (*REG32(CM_DPICTL) & CM_DPICTL_BUSY_SET) {};
#elif defined(GERTVGA)
  *REG32(CM_DPIDIV) = CM_PASSWORD | (0xc00 << 4);
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
  printf("DPI clock set\n");
  int rate = measure_clock(17);
  printf("DPI clock measured at %d KHz, ", rate/1000);

  int htotal = t.hfp + t.hsync + t.hbp + t.hactive;
  int vtotal = t.vfp + t.vsync + t.vbp + t.vactive;
  printf("hsync rate: %d Hz, ", rate / htotal);
  printf("vsync rate: %d Hz, ", rate / (htotal * vtotal));
  printf("htotal: %d, vtotal: %d\n", htotal, vtotal);

  setup_pixelvalve(&t, 0);

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
    if (i == 14) continue;
    if (i == 15) continue;
    gpio_config(i, kBCM2708Pinmux_ALT2);
  }
  for (int i=16; i<=21; i++) {
    gpio_config(i, kBCM2708Pinmux_ALT2);
  }
  hvs_set_background_color(0, 0xff0000);
#endif
  return 0;
}

static enum handler_return updater_entry(struct timer *t, lk_time_t now, void *arg) {
  for (int y=0; y<height; y++) {
    for (int x=0; x<width; x++) {
      gfx_putpixel(framebuffer, x, y, 0xff000000 | (count << 16));
    }
  }
  count++;
  return INT_NO_RESCHEDULE;
}

int cmd_dpi_count(int argc, const console_cmd_args *argv) {
  timer_initialize(&updater);
  timer_set_periodic(&updater, 1000, updater_entry, NULL);
  return 0;
}

static enum handler_return mover_entry(struct timer *t, lk_time_t now, void *arg) {
  int y = count2 / width;
  int x = count2 % width;
  gfx_putpixel(framebuffer, x, y, 0xff000000);
  count2 = (count2+1) % (width*height);
  y = count2 / width;
  x = count2 % width;
  gfx_putpixel(framebuffer, x, y, 0xffffffff);
  return INT_NO_RESCHEDULE;
}

int cmd_dpi_move(int argc, const console_cmd_args *argv) {
  timer_initialize(&updater);
  timer_set_periodic(&updater, 10, mover_entry, NULL);
  return 0;
}

static void dpi_init(uint level) {
  cmd_dpi_start(0, NULL);
}

LK_INIT_HOOK(dpi, &dpi_init, LK_INIT_LEVEL_PLATFORM - 1);
