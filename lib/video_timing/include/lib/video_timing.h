#pragma once

#include <platform/bcm28xx/pv.h>

#define kHz 1000
#define MHz 1000000

static const struct pv_timings pv_480p60 = {
  .hactive = 640,  .hfp = 60,  .hsync = 64, .hbp = 80,
  .vactive = 480,  .vfp =  3,  .vsync =  4, .vbp = 13,
  .interlaced = false,
  .fps = 60,
};

static const struct pv_timings pv_720p60 = {
  .hactive = 1280, .hfp = 110, .hsync = 40, .hbp = 220,
  .vactive = 720,  .vfp =   5, .vsync =  5, .vbp =  20,
  .interlaced = false,
  .fps = 60,
  .pixel_clock = 74250 * kHz
};

// 1920x1080@60 CEA-861/DMT timings
static const struct pv_timings pv_1080p60 = {
  .hactive = 1920, .hfp = 88, .hsync = 44, .hbp = 148,
  .vactive = 1080, .vfp =  4, .vsync =  5, .vbp =  36,
  .interlaced = false,
  .fps = 60,
  .pixel_clock = 148500 * kHz,
};

void print_timing_debug(const struct pv_timings *t);

static uint32_t get_total_pixels(const struct pv_timings *t) {
  int htotal = t->hfp + t->hsync + t->hbp + t->hactive;
  int vtotal = t->vfp + t->vsync + t->vbp + t->vactive;
  int total_pixels = htotal * vtotal;
  return total_pixels;
}

static uint32_t get_pixel_clock(const struct pv_timings *t) {
  if (t->pixel_clock) return t->pixel_clock;

  uint32_t total_pixels = get_total_pixels(t);
  return total_pixels * t->fps;
}
