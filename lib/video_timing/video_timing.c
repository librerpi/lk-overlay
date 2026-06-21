#include <lib/video_timing.h>
#include <stdio.h>

void print_timing_debug(const struct pv_timings *t) {
  int htotal = t->hfp + t->hsync + t->hbp + t->hactive;
  int vtotal = t->vfp + t->vsync + t->vbp + t->vactive;
  int total_pixels = htotal * vtotal;
  int pclk = total_pixels * t->fps;
  printf("hsync rate: %d Hz, ", pclk / htotal);
  printf("vsync rate: %d Hz, ", pclk / total_pixels);
  printf("htotal: %d, vtotal: %d\n", htotal, vtotal);
}
