#ifdef ENABLE_TEXT
# include <lib/font.h>
#endif
#include <arch/ops.h>
#include <assert.h>
#include <dev/display.h>
#include <kernel/timer.h>
#include <lk/console_cmd.h>
#include <lk/err.h>
#include <lk/init.h>
#include <lk/reg.h>
#include <platform/bcm28xx/clock.h>
#include <platform/bcm28xx/hvs.h>
#include <platform/bcm28xx/pv.h>
#include <platform/interrupts.h>
#include <stdio.h>
#include <stdlib.h>
#include <strings.h>

// note, 4096 slots total
#ifdef RPI4
volatile uint32_t* dlist_memory = REG32(SCALER5_LIST_MEMORY);
#else
volatile uint32_t* dlist_memory = REG32(SCALER_LIST_MEMORY);
#endif
volatile struct hvs_channel *hvs_channels = (volatile struct hvs_channel*)REG32(SCALER_DISPCTRL0);
int display_slot = 0;
int scaled_layer_count = 0;
timer_t ddr2_monitor;

enum scaling_mode {
  none,
  PPF, // upscaling?
  TPZ // downscaling?
};

struct hvs_channel_config channels[3];

gfx_surface *debugText;

static int cmd_hvs_dump(int argc, const console_cmd_args *argv);
static int cmd_hvs_update(int argc, const console_cmd_args *argv);

STATIC_COMMAND_START
STATIC_COMMAND("hvs_dump", "dump hvs state", &cmd_hvs_dump)
STATIC_COMMAND("hvs_dump_dlist", "dump the software dlist", &cmd_hvs_dump_dlist)
STATIC_COMMAND("hvs_update", "update the display list, without waiting for irq", &cmd_hvs_update)
STATIC_COMMAND_END(hvs);

static uint32_t gfx_to_hvs_pixel_format(gfx_format fmt) {
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

#ifdef RPI4
void hvs_add_plane(gfx_surface *fb, int x, int y, bool hflip) {
  assert(fb);
  printf("rendering FB of size %dx%d at %dx%d at %d\n", fb->width, fb->height, x, y, display_slot);
#if 0
  dlist_memory[display_slot++] = CONTROL_VALID
    | CONTROL_WORDS(8)
    | CONTROL_PIXEL_ORDER(HVS_PIXEL_ORDER_ABGR)
    | (hflip ? CONTROL0_HFLIP : 0)
    | (1<<15) // unity scaling
    | (1<<11) // rgb expand
    | (1<<12) // alpha expand
    | CONTROL_FORMAT(gfx_to_hvs_pixel_format(fb->format));
  dlist_memory[display_slot++] = POS0_X(x) | (y << 16);
  // control word 2
  dlist_memory[display_slot++] = (10 << 4); // alpha?
  /* Position Word 2: Source Image Size */
  dlist_memory[display_slot++] = POS2_H(fb->height) | POS2_W(fb->width);
  /* Position Word 3: Context.  Written by the HVS. */
  dlist_memory[display_slot++] = 0xDEADBEEF; // dummy for HVS state
  dlist_memory[display_slot++] = (uint32_t)fb->ptr | 0xc0000000;
  dlist_memory[display_slot++] = 0xDEADBEEF; // dummy for HVS state
  dlist_memory[display_slot++] = fb->stride * fb->pixelsize;
#endif
  dlist_memory[display_slot++] = 0x4800d807;
  dlist_memory[display_slot++] = 0x00000000;
  dlist_memory[display_slot++] = 0x4000fff0; // control 2
  dlist_memory[display_slot++] = 0x04000500; // size
  dlist_memory[display_slot++] = 0x01aa0000; // state
  dlist_memory[display_slot++] = 0xdfa00000; // ptr word
  dlist_memory[display_slot++] = 0xdfc14800; // state
  dlist_memory[display_slot++] = 0x00001400; // stride
}
#else
void hvs_add_plane(gfx_surface *fb, int x, int y, bool hflip) {
  assert(fb);
  //printf("rendering FB of size %dx%d at %dx%d\n", fb->width, fb->height, x, y);
  dlist_memory[display_slot++] = CONTROL_VALID
    | CONTROL_WORDS(7)
    | CONTROL_PIXEL_ORDER(HVS_PIXEL_ORDER_ABGR)
//    | CONTROL0_VFLIP // makes the HVS addr count down instead, pointer word must be last line of image
    | (hflip ? CONTROL0_HFLIP : 0)
    | CONTROL_UNITY
    | CONTROL_FORMAT(gfx_to_hvs_pixel_format(fb->format));
  dlist_memory[display_slot++] = POS0_X(x) | POS0_Y(y) | POS0_ALPHA(0xff);
  dlist_memory[display_slot++] = POS2_H(fb->height) | POS2_W(fb->width) | (1 << 30); // TODO SCALER_POS2_ALPHA_MODE_FIXED
  dlist_memory[display_slot++] = 0xDEADBEEF; // dummy for HVS state
  dlist_memory[display_slot++] = (uint32_t)fb->ptr | 0xc0000000;
  dlist_memory[display_slot++] = 0xDEADBEEF; // dummy for HVS state
  dlist_memory[display_slot++] = fb->stride * fb->pixelsize;
}
#endif

static void write_tpz(unsigned int source, unsigned int dest) {
  uint32_t scale = (1<<16) * source / dest;
  uint32_t recip = ~0 / scale;
  dlist_memory[display_slot++] = scale << 8;
  dlist_memory[display_slot++] = recip;
}

void hvs_add_plane_scaled(gfx_surface *fb, int x, int y, unsigned int width, unsigned int height, bool hflip) {
  assert(fb);
  if ((x < 0) || (y < 0)) printf("rendering FB of size %dx%d at %dx%d, scaled down to %dx%d\n", fb->width, fb->height, x, y, width, height);
  enum scaling_mode xmode, ymode;
  if (fb->width > width) xmode = TPZ;
  else if (fb->width < width) xmode = PPF;
  else xmode = none;

  if (fb->height > height) ymode = TPZ;
  else if (fb->height < height) ymode = PPF;
  else ymode = none;

  int scl0;
  switch ((xmode << 2) | ymode) {
  case (PPF << 2) | PPF:
    scl0 = SCALER_CTL0_SCL_H_PPF_V_PPF;
    break;
  case (TPZ << 2) | PPF:
    scl0 = SCALER_CTL0_SCL_H_TPZ_V_PPF;
    break;
  case (PPF << 2) | TPZ:
    scl0 = SCALER_CTL0_SCL_H_PPF_V_TPZ;
    break;
  case (TPZ << 2) | TPZ:
    scl0 = SCALER_CTL0_SCL_H_TPZ_V_TPZ;
    break;
  default:
    puts("unsupported scale combination");
  }

  int start = display_slot;
  dlist_memory[display_slot++] = 0 // CONTROL_VALID
//    | CONTROL_WORDS(14)
    | CONTROL_PIXEL_ORDER(HVS_PIXEL_ORDER_ABGR)
//    | CONTROL0_VFLIP // makes the HVS addr count down instead, pointer word must be last line of image
    | (hflip ? CONTROL0_HFLIP : 0)
    | CONTROL_FORMAT(gfx_to_hvs_pixel_format(fb->format))
    | (scl0 << 5)
    | (scl0 << 8); // SCL1
  dlist_memory[display_slot++] = POS0_X(x) | POS0_Y(y) | POS0_ALPHA(0xff);  // position word 0
  dlist_memory[display_slot++] = width | (height << 16);                    // position word 1
  dlist_memory[display_slot++] = POS2_H(fb->height) | POS2_W(fb->width);    // position word 2
  dlist_memory[display_slot++] = 0xDEADBEEF;                                // position word 3, dummy for HVS state
  dlist_memory[display_slot++] = (uint32_t)fb->ptr | 0x80000000;            // pointer word 0
  dlist_memory[display_slot++] = 0xDEADBEEF;                                // pointer context word 0 dummy for HVS state
  dlist_memory[display_slot++] = fb->stride * fb->pixelsize;                // pitch word 0
  dlist_memory[display_slot++] = (scaled_layer_count * 720);         // LBM base addr
  scaled_layer_count++;

#if 0
  bool ppf = false;
  if (ppf) {
    uint32_t xscale = (1<<16) * fb->width / width;
    uint32_t yscale = (1<<16) * fb->height / height;

    dlist_memory[display_slot++] = SCALER_PPF_AGC | (xscale << 8);
    dlist_memory[display_slot++] = SCALER_PPF_AGC | (yscale << 8);
    dlist_memory[display_slot++] = 0xDEADBEEF; //scaling context
  }
#endif

  if (xmode == PPF) {
    puts("unfinished");
  }

  if (ymode == PPF) {
    puts("unfinished");
  }

  if (xmode == TPZ) {
    write_tpz(fb->width, width);
  }

  if (ymode == TPZ) {
    write_tpz(fb->height, height);
    dlist_memory[display_slot++] = 0xDEADBEEF; // context for scaling
  }

  //printf("entry size: %d, spans 0x%x-0x%x\n", display_slot - start, start, display_slot);
  dlist_memory[start] |= CONTROL_VALID | CONTROL_WORDS(display_slot - start);
}

void hvs_terminate_list(void) {
  //printf("adding termination at %d\n", display_slot);
  dlist_memory[display_slot++] = CONTROL_END;
  if (scaled_layer_count > 10) scaled_layer_count = 0;
}

static enum handler_return hvs_irq(void *unused) {
  puts("hvs interrupt");
  return INT_NO_RESCHEDULE;
}

//#define SD_IDL 0x7ee00018
//#define SD_CYC 0x7ee00030

static void check_sdram_usage(void) {
  static float last_time = 1;
  uint64_t idle = *REG32(SD_IDL);
  uint64_t total = *REG32(SD_CYC);
  *REG32(SD_IDL) = 0;
  uint32_t idle_percent = (idle * 100) / (total);
  float time = ((float)*REG32(ST_CLO)) / 1000000;
  uint32_t clock = total / (time - last_time) / 1000 / 1000;
  last_time = time;
  printf("[%f] DDR2 was idle 0x%x / 0x%x cycles (%d%%) %dMHz\n", time, (uint32_t)idle, (uint32_t)total, idle_percent, clock);
}

static enum handler_return ddr2_checker(struct timer *unused1, unsigned int unused2,  void *unused3) {
  check_sdram_usage();
  return INT_NO_RESCHEDULE;
}

#define DSP3_MUX(n) ((n & 0x3) << 18)

void hvs_initialize() {
  static bool initialized = false;
  if (initialized) return;
  initialized = true;

  puts("hvs_initialize()");

  timer_initialize(&ddr2_monitor);
  //timer_set_periodic(&ddr2_monitor, 500, ddr2_checker, NULL);

#ifdef ENABLE_TEXT
  debugText = gfx_create_surface(NULL, FONT_X * 10, FONT_Y, FONT_X * 10, GFX_FORMAT_ARGB_8888);
  hvs_layer *debugTextLayer = malloc(sizeof(hvs_layer));
  MK_UNITY_LAYER(debugTextLayer, debugText, 1000, 50, 50);
  debugTextLayer->name = "debug text";
#endif

  int hvs_channel = 1;
  *REG32(SCALER_DISPCTRL) &= ~SCALER_DISPCTRL_ENABLE; // disable HVS
  *REG32(SCALER_DISPCTRL) = SCALER_DISPCTRL_ENABLE // re-enable HVS
    | DSP3_MUX(3)
    | 0x7f; // irq en
  for (int i=0; i<3; i++) {
    hvs_channels[i].dispctrl = SCALER_DISPCTRLX_RESET;
    hvs_channels[i].dispctrl = 0;
    hvs_channels[i].dispbkgnd = 0x1020202; // bit 24
  }

#ifdef ENABLE_TEXT
  mutex_acquire(&channels[hvs_channel].lock);
  hvs_dlist_add(hvs_channel, debugTextLayer);
  mutex_release(&channels[hvs_channel].lock);
#endif

  hvs_channels[2].dispbase = BASE_BASE(0)      | BASE_TOP(0x7f0);
  hvs_channels[1].dispbase = BASE_BASE(0xf10)  | BASE_TOP(0x50f0);
  hvs_channels[0].dispbase = BASE_BASE(0x800) | BASE_TOP(0xf00);

  hvs_wipe_displaylist();

  *REG32(SCALER_DISPEOLN) = 0x40000000;
}

void hvs_setup_irq() {
  register_int_handler(33, hvs_irq, NULL);
  unmask_interrupt(33);
}

uint32_t hsync, hbp, hact, hfp, vsync, vbp, vfps, last_vfps;

static enum handler_return pv_irq(void *pvnr) {
  enum handler_return ret = INT_NO_RESCHEDULE;
  int hvs_channel = 1;
#ifdef RPI4
  // FIXME
#else
  if (pvnr == 0) hvs_channel = 0;
  else if (pvnr == 2) hvs_channel = 1;
#endif
  uint32_t t = *REG32(ST_CLO);
  struct pixel_valve *rawpv = getPvAddr((int)pvnr);
  uint32_t stat = rawpv->int_status;
  uint32_t ack = 0;
  uint32_t stat1 = hvs_channels[hvs_channel].dispstat;
  if (stat & PV_INTEN_HSYNC_START) {
    ack |= PV_INTEN_HSYNC_START;
    hsync = t;
    if ((SCALER_STAT_LINE(stat1) % 5) == 0) {
      //hvs_set_background_color(1, 0x0000ff);
    } else {
      //hvs_set_background_color(1, 0xffffff);
    }
  }
  if (stat & PV_INTEN_HBP_START) {
    ack |= PV_INTEN_HBP_START;
    hbp = t;
  }
  if (stat & PV_INTEN_HACT_START) {
    ack |= PV_INTEN_HACT_START;
    hact = t;
  };
  if (stat & PV_INTEN_HFP_START) {
    ack |= PV_INTEN_HFP_START;
    hfp = t;
  }
  if (stat & PV_INTEN_VSYNC_START) {
    ack |= PV_INTEN_VSYNC_START;
    vsync = t;
  }
  if (stat & PV_INTEN_VBP_START) {
    ack |= PV_INTEN_VBP_START;
    vbp = t;
  }
  if (stat & PV_INTEN_VACT_START) {
    ack |= PV_INTEN_VACT_START;
  }
  if (stat & PV_INTEN_VFP_START) {
    ack |= PV_INTEN_VFP_START;
    last_vfps = vfps;
    vfps = t;

    // actually do the page-flip
    if (hvs_channel == 0) {
      *REG32(SCALER_DISPLIST0) = channels[hvs_channel].dlist_target;
    } else if (hvs_channel == 1) {
      *REG32(SCALER_DISPLIST1) = channels[hvs_channel].dlist_target;
    }

    THREAD_LOCK(state);
    int woken = wait_queue_wake_all(&channels[hvs_channel].vsync, false, NO_ERROR);
    if (woken > 0) ret = INT_RESCHEDULE;
    THREAD_UNLOCK(state);

    //hvs_set_background_color(1, 0xff0000);
    //do_frame_update((stat1 >> 12) & 0x3f);
    //printf("line: %d frame: %2d start: %4d ", stat1 & 0xfff, (stat1 >> 12) & 0x3f, *REG32(SCALER_DISPLIST1));
    //uint32_t idle = *REG32(SD_IDL);
    //uint32_t total = *REG32(SD_CYC);
    //*REG32(SD_IDL) = 0;
    //uint64_t idle_percent = ((uint64_t)idle * 100) / ((uint64_t)total);
    //printf("sdram usage: %d %d, %lld\n", idle, total, idle_percent);
    //printf("HSYNC:%5d HBP:%d HACT:%d HFP:%d VSYNC:%5d VBP:%5d VFPS:%d FRAME:%d\n", t - vsync, t-hbp, t-hact, t-hfp, t-vsync, t-vbp, t-vfps, t-last_vfps);
    //hvs_set_background_color(1, 0xffffff);
  }
  if (stat & PV_INTEN_VFP_END) {
    ack |= PV_INTEN_VFP_END;
  }
  if (stat & PV_INTEN_IDLE) {
    ack |= PV_INTEN_IDLE;
  }
  rawpv->int_status = ack;
  return ret;
}

void hvs_configure_channel(int channel, int width, int height, bool interlaced) {
  printf("hvs_configure_channel(%d, %d, %d, %s)\n", channel, width, height, interlaced ? "true" : "false");
  channels[channel].width = width;
  channels[channel].height = height;
  channels[channel].interlaced = interlaced;

  hvs_channels[channel].dispctrl = SCALER_DISPCTRLX_RESET;
  hvs_channels[channel].dispctrl = SCALER_DISPCTRLX_ENABLE | SCALER_DISPCTRL_W(width) | SCALER_DISPCTRL_H(height);

  hvs_channels[channel].dispbkgnd = SCALER_DISPBKGND_AUTOHS | 0x020202
    | (channels[channel].interlaced ? SCALER_DISPBKGND_INTERLACE : 0);
  // the SCALER_DISPBKGND_INTERLACE flag makes the HVS alternate between sending even and odd scanlines

  channels[channel].dlist_target = 0;
  if (true) {
    puts("setting up pv interrupt");
    int pvnr = 2;
    if (channel == 0) pvnr = 0;
    if (channel == 1) pvnr = 2;

    struct pixel_valve *rawpv = getPvAddr(pvnr);
    rawpv->int_enable = 0;
    rawpv->int_status = 0xff;
    setup_pv_interrupt(pvnr, pv_irq, (void*)pvnr);
    rawpv->int_enable = PV_INTEN_VFP_START | 0x3f;
    //hvs_setup_irq();
    puts("done");
  }
}

void hvs_wipe_displaylist(void) {
  for (int i=0; i<1024; i++) {
    dlist_memory[i] = CONTROL_END;
  }
  display_slot = 0;
}

static bool bcm_host_is_model_pi4(void) {
  return false;
}

void hvs_print_position0(uint32_t w) {
  printf("position0: 0x%x\n", w);
  if (bcm_host_is_model_pi4()) {
    printf("    x: %d y: %d\n", w & 0x3fff, (w >> 16) & 0x3fff);
  } else {
    printf("    x: %d y: %d\n", w & 0xfff, (w >> 12) & 0xfff);
  }
}
void hvs_print_control2(uint32_t w) {
  printf("control2: 0x%x\n", w);
  printf("  alpha: 0x%x\n", (w >> 4) & 0xffff);
  printf("  alpha mode: %d\n", (w >> 30) & 0x3);
}
void hvs_print_word1(uint32_t w) {
  printf("  word1: 0x%x\n", w);
}
void hvs_print_position2(uint32_t w) {
  printf("position2: 0x%x\n", w);
  printf("  width: %d height: %d\n", w & 0xffff, (w >> 16) & 0xfff);
}
void hvs_print_position3(uint32_t w) {
  printf("position3: 0x%x\n", w);
}
void hvs_print_pointer0(uint32_t w) {
  printf("pointer word: 0x%x\n", w);
}
void hvs_print_pointerctx0(uint32_t w) {
  printf("pointer context word: 0x%x\n", w);
}
void hvs_print_pitch0(uint32_t w) {
  printf("pitch word: 0x%x\n", w);
}

static int cmd_hvs_dump(int argc, const console_cmd_args *argv) {
  printf("SCALER_DISPCTRL: 0x%x\n", *REG32(SCALER_DISPCTRL));
  printf("SCALER_DISPSTAT: 0x%x\n", *REG32(SCALER_DISPSTAT));
  printf("SCALER_DISPEOLN: 0x%08x\n", *REG32(SCALER_DISPEOLN));
  printf("SCALER_DISPLIST0: 0x%x\n", *REG32(SCALER_DISPLIST0));
  uint32_t list1 = *REG32(SCALER_DISPLIST1);
  printf("SCALER_DISPLIST1: 0x%x\n", list1);
  printf("SCALER_DISPLIST2: 0x%x\n\n", *REG32(SCALER_DISPLIST2));
  for (int i=0; i<3; i++) {
    if ((argc > 1) && (argv[1].u != i)) continue;
    printf("SCALER_DISPCTRL%d: 0x%x\n", i, hvs_channels[i].dispctrl);
    printf("  width: %d\n", (hvs_channels[i].dispctrl >> 12) & 0xfff);
    printf("  height: %d\n", hvs_channels[i].dispctrl & 0xfff);
    printf("SCALER_DISPBKGND%d: 0x%x\n", i, hvs_channels[i].dispbkgnd);
    uint32_t stat = hvs_channels[i].dispstat;
    printf("SCALER_DISPSTAT%d: 0x%x\n", i, stat);
    printf("mode: %d\n", (stat >> 30) & 0x3);
    if (stat & (1<<29)) puts("full");
    if (stat & (1<<28)) puts("empty");
    printf("unknown: 0x%x\n", (stat >> 18) & 0x3ff);
    printf("frame count: %d\n", (stat >> 12) & 0x3f);
    printf("line: %d\n", SCALER_STAT_LINE(stat));
    uint32_t base = hvs_channels[i].dispbase;
    printf("SCALER_DISPBASE%d: base 0x%x top 0x%x\n\n", i, base & 0xffff, base >> 16);
  }
  for (uint32_t i=list1; i<(list1+64); i++) {
    printf("dlist[%x]: 0x%x\n", i, dlist_memory[i]);
    if (dlist_memory[i] & BV(31)) {
      puts("(31)END");
      break;
    }
    if (dlist_memory[i] & BV(30)) {
      int x = i;
      int words = (dlist_memory[i] >> 24) & 0x3f;
      for (unsigned int index=i; index < (i+words); index++) {
        printf("raw dlist[%d] == 0x%x\n", index-i, dlist_memory[index]);
      }
      bool unity;
      printf("  (3:0)format: %d\n", dlist_memory[i] & 0xf);
      if (dlist_memory[i] & (1<<4)) puts("  (4)unity");
      printf("  (7:5)SCL0: %d\n", (dlist_memory[i] >> 5) & 0x7);
      printf("  (10:8)SCL1: %d\n", (dlist_memory[i] >> 8) & 0x7);
      if (false) { // is bcm2711
        if (dlist_memory[i] & (1<<11)) puts("  (11)rgb expand");
        if (dlist_memory[i] & (1<<12)) puts("  (12)alpha expand");
      } else {
        printf("  (12:11)rgb expand: %d\n", (dlist_memory[i] >> 11) & 0x3);
      }
      printf("  (14:13)pixel order: %d\n", (dlist_memory[i] >> 13) & 0x3);
      if (false) { // is bcm2711
        unity = dlist_memory[i] & (1<<15);
      } else {
        unity = dlist_memory[i] & (1<<4);
        if (dlist_memory[i] & (1<<15)) puts("  (15)vflip");
        if (dlist_memory[i] & (1<<16)) puts("  (16)hflip");
      }
      printf("  (18:17)key mode: %d\n", (dlist_memory[i] >> 17) & 0x3);
      if (dlist_memory[i] & (1<<19)) puts("  (19)alpha mask");
      printf("  (21:20)tiling mode: %d\n", (dlist_memory[i] >> 20) & 0x3);
      printf("  (29:24)words: %d\n", words);
      x++;
      hvs_print_position0(dlist_memory[x++]);
      if (bcm_host_is_model_pi4()) {
        hvs_print_control2(dlist_memory[x++]);
      }
      if (unity) {
        puts("unity scaling");
      } else {
        hvs_print_word1(dlist_memory[x++]);
      }
      hvs_print_position2(dlist_memory[x++]);
      hvs_print_position3(dlist_memory[x++]);
      hvs_print_pointer0(dlist_memory[x++]);
      hvs_print_pointerctx0(dlist_memory[x++]);
      hvs_print_pitch0(dlist_memory[x++]);
      if (words > 1) {
        i += words - 1;
      }
    }
  }
  return 0;
}

__WEAK status_t display_get_framebuffer(struct display_framebuffer *fb) {
  //return ERR_NOT_SUPPORTED;
  puts("creating framebuffer for text console\n");

  int w = 640;
  int h = 210;
  struct gfx_surface *gfx = gfx_create_surface(NULL, w, h, w, GFX_FORMAT_ARGB_8888);
  bzero(gfx->ptr, gfx->len);
  fb->image.pixels = gfx->ptr;
  fb->format = DISPLAY_FORMAT_ARGB_8888;
  fb->image.format = IMAGE_FORMAT_ARGB_8888;
  fb->image.rowbytes = w * 4;
  fb->image.width = w;
  fb->image.height = h;
  fb->image.stride = w;
  fb->flush = NULL;
  hvs_layer *console_layer = malloc(sizeof(hvs_layer));
  MK_UNITY_LAYER(console_layer, gfx, 50, 50, 30);
  console_layer->name = "console";

  int channel = 1;
  mutex_acquire(&channels[channel].lock);
  hvs_dlist_add(channel, console_layer);
  hvs_update_dlist(channel);
  mutex_release(&channels[channel].lock);


  console_layer = malloc(sizeof(hvs_layer));
  MK_UNITY_LAYER(console_layer, gfx, 50, 50, 130);
  console_layer->name = "console0";
  channel=0;
  mutex_acquire(&channels[channel].lock);
  hvs_dlist_add(channel, console_layer);
  hvs_update_dlist(channel);
  mutex_release(&channels[channel].lock);

  return NO_ERROR;
}

static int cmd_hvs_update(int argc, const console_cmd_args *argv) {
  int channel = 1;
  if (argc >= 2) channel = argv[1].u;

  mutex_acquire(&channels[channel].lock);
  cmd_hvs_dump_dlist(0, NULL);
  hvs_update_dlist(channel);
  mutex_release(&channels[channel].lock);
  return 0;
}

void hvs_update_dlist(int channel) {
  assert(is_mutex_held(&channels[channel].lock));
  hvs_layer *layer;

  uint32_t t = *REG32(ST_CLO);
  printf("doing dlist update at %d\n", t);

  int list_start = display_slot;

  list_for_every_entry(&channels[channel].layers, layer, hvs_layer, node) {
    if ((layer->w == layer->fb->width) && (layer->h == layer->fb->height)) { // unity scale
      hvs_add_plane(layer->fb, layer->x, layer->y, false);
    } else {
      hvs_add_plane_scaled(layer->fb, layer->x, layer->y, layer->w, layer->h, false);
    }
  }

  hvs_terminate_list();

#ifdef ENABLE_TEXT
  if (1) {
    char buffer[11];
    bzero(buffer, 10);
    //bzero(debugText->ptr, debugText->len);
    uint32_t *t = (uint32_t*)debugText->ptr;
    for (int i=0; i<(debugText->len/4); i++) {
      t[i] = 0xff000000;
    }
    snprintf(buffer, 10, "%d", display_slot);
    const char *c;
    int x = 0;
    for (c = buffer; *c; c++) {
      font_draw_char(debugText, *c, x, 0, 0xffffffff);
      x += FONT_X;
    }
  }
#endif

  if (display_slot > 4096) {
    printf("dlist overflow!!!: %d\n", display_slot);
    display_slot = 0;
    hvs_update_dlist(channel);
    return;
  }

  if (display_slot > 4000) {
    display_slot = 0;
    puts("dlist loop");
  }

  channels[channel].dlist_target = list_start;
}

int cmd_hvs_dump_dlist(int argc, const console_cmd_args *argv) {
  int start = 1, end = 1;
  if (argc >= 2) {
    start = argv[1].u;
    end = start;
  }
  if (argc >= 3) end = argv[2].u;
  for (int channel = start; channel <= end; channel++) {
    printf("hvs channel %d, dlist start %d\n", channel, channels[channel].dlist_target);
    hvs_layer *layer;
    list_for_every_entry(&channels[channel].layers, layer, hvs_layer, node) {
      printf("%p %p %3d,%3d + %3dx%3d, layer:%4d %s\n", layer, layer->fb->ptr
          , layer->x, layer->y
          , layer->w, layer->h
          , layer->layer, layer->name ? layer->name : "NULL");
    }
  }
  return 0;
}

void hvs_dlist_add(int channel, hvs_layer *new_layer) {
  hvs_layer *layer;
  //printf("adding at layer %d\n", new_layer->layer);
  list_for_every_entry(&channels[channel].layers, layer, hvs_layer, node) {
    //printf("current is %d\n", layer->layer);
    if (layer->layer == new_layer->layer) {
      puts("match insert");
      list_add_after(&layer->node, &new_layer->node);
      return;
    } else if (layer->layer > new_layer->layer) {
      //puts("past insert");
      list_add_before(&layer->node, &new_layer->node);
      return;
    }
  }
  puts("no match insert");
  list_add_tail(&channels[channel].layers, &new_layer->node);
}

static void hvs_init_hook(uint level) {
  puts("hvs_init_hook()\n");
  for (int i=0; i<3; i++) {
    list_initialize(&channels[i].layers);
    mutex_init(&channels[i].lock);
    wait_queue_init(&channels[i].vsync);
  }
}

uint32_t hvs_wait_vsync(int hvs_channel) {
  THREAD_LOCK(state);
  wait_queue_block(&channels[hvs_channel].vsync, INFINITE_TIME);
  uint32_t stat = hvs_channels[hvs_channel].dispstat;
  THREAD_UNLOCK(state);
  return stat;
}

LK_INIT_HOOK(hvs, &hvs_init_hook, LK_INIT_LEVEL_PLATFORM - 2);
