#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef struct {
  uint16_t pixel_clock;
  uint8_t hactive_lo;
  uint8_t hblank_lo;
  uint8_t hextra; // upper 4bits of hactive and hblank
  uint8_t vactive_lo;
  uint8_t vblank_lo;
  uint8_t vextra;
  uint8_t hfp_offset_lo;
  uint8_t hsync_offset_lo;
  uint8_t vblank_extra;
  uint8_t blank_extra;
  uint8_t hsize_lo;
  uint8_t vsize_lo;
  uint8_t size_extra;
  uint8_t hborder;
  uint8_t vborder;
  uint8_t features_bitmap;
} detailed_timing_t;

typedef struct {
  uint16_t res0;
  uint8_t res1;
  uint8_t descriptor_type;
  uint8_t res2;
  uint8_t body[13];
} monitor_descriptor_t;

typedef struct {
  uint8_t header[8];
  uint16_t manufacturer; // BE, 3 5bit chars, 1=A
  uint16_t manufacturer_code;
  uint32_t serial; // LE
  uint8_t week;
  uint8_t year; // 1990 epoch
  uint8_t edid_version;
  uint8_t edid_revision;

  uint8_t video_param_bitmap;
  uint8_t horizontal_size;
  uint8_t vertical_size;
  uint8_t display_gamma;
  uint8_t features_bitmap;
  uint8_t chromaticity_dummy[10];
  uint8_t established_timings[3];
  uint16_t standard_timing[8];
  detailed_timing_t detailed_timings[4];
  uint8_t extension_count;
  uint8_t checksum;
} edid_t;

void edid_pretty_print(const edid_t *e);
bool edid_check_checksum(const edid_t *e);
