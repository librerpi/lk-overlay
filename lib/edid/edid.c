#include <lib/edid.h>
#include <stdio.h>
#include <platform/bcm28xx.h>
#include <lib/video_timing.h>

static const char *aspects[4] = { "16:10", "4:3", "5:4", "16:9" };

void edid_pretty_print(const edid_t *e) {
  printf("year: %d\n", 1990 + e->year);
  printf("size: %dcm x %dcm\n", e->horizontal_size, e->vertical_size);
  printf("established timings: 0x%x 0x%x 0x%x\n", e->established_timings[0], e->established_timings[1], e->established_timings[2]);
  for (int i=0; i<8; i++) {
    if (e->standard_timing[i] == 0x0101) continue;
    uint8_t byte0 = e->standard_timing[i] & 0xff;
    uint8_t byte1 = e->standard_timing[i] >> 8;
    int width = (byte0 + 31) * 8;
    int aspect = (byte1 >> 6) & 3;
    int vfreq = (byte1 & 0x3f) + 60;
    printf("standard timings %d: 0x%x, width %d, %s, %d Hz\n", i, e->standard_timing[i], width, aspects[aspect], vfreq);
  }

  for (int i=0; i<4; i++) {
    detailed_timing_t *d = &e->detailed_timings[i];
    if (d->pixel_clock == 0) {
      monitor_descriptor_t *m = &e->detailed_timings[i];
      printf("  monitor descriptor 0x%x\n", m->descriptor_type);
      switch (m->descriptor_type) {
      case 0xfc:
        printf("    name: %s\n", m->body);
        break;
      case 0xfd:
        printf("    monitor range limits\n");
        break;
      case 0xff:
        printf("    serial: %s\n", m->body);
        break;
      }
    } else {
      if (i == 0) puts("prefered timing:");
      else printf("detailed timing %d\n", i);
      printf("  pclk: %d0 kHz\n", d->pixel_clock);
      int hactive = d->hactive_lo | ((d->hextra & 0xf0) << 4);
      int hblank = d->hblank_lo | ((d->hextra & 0x0f) << 8);
      printf("  hactive %d, hblank %d\n", hactive, hblank);
      int vactive = d->vactive_lo | ((d->vextra & 0xf0) << 4);
      int vblank = d->vblank_lo | ((d->vextra & 0x0f) << 8);
      printf("  vactive %d, vblank %d\n", vactive, vblank);

      int hfp = d->hfp_offset_lo | ((d->blank_extra & 0xc0) << 2);
      int hsync = d->hsync_offset_lo | ((d->blank_extra & 0x30) << 4);
      printf("  hfp %d, hsync %d\n", hfp, hsync);

      int vfp = ((d->vblank_extra & 0xf0) >> 4) | ((d->blank_extra & 0x0c) << 2);
      int vsync = (d->vblank_extra & 0x0f) | ((d->blank_extra & 0x03) << 4);
      printf("  vfp %d, vsync %d\n", vfp, vsync);

      int hsize = d->hsize_lo | ((d->size_extra & 0xf0) << 4);
      int vsize = d->vsize_lo | ((d->size_extra & 0x0f) << 8);
      printf("  size: %d mm x %d mm\n", hsize, vsize);
      uint8_t features = d->features_bitmap;
      if (features & BV(7)) puts("  interlaced");
      int stereo_mode = ((features >> 4) & 6) | (features & BV(0));
      if (stereo_mode) printf("  stereo mode: %d\n", stereo_mode);
      if ((features & BV(4)) == 0) {
        puts("  analog sync");
        if (features & BV(3)) puts("    bipolar analog composite");
        if (features & BV(2)) puts("    with serrations");
        if (features & BV(1)) puts("    sync on all channels");
      } else if (((features >> 3) & 3) == 2) {
        puts("  digital sync composite");
      } else if (((features >> 3) & 3) == 3) {
        puts("  digital sync seperate");
        if (features & BV(2)) puts("    vsync positive");
        if (features & BV(1)) puts("    hsync positive");
      }

      struct pv_timings t = {
        .hactive = hactive,
        .hfp = hfp,
        .hsync = hsync,
        .hbp = hblank - hfp - hsync,
        .vactive = vactive,
        .vfp = vfp,
        .vsync = vsync,
        .vbp = vblank - vfp - vsync,
        .interlaced = false,
        .fps = 60, // TODO
        .pixel_clock = d->pixel_clock * 10000,
      };
      print_timing_debug(&t);
    }
  }

  printf("extensions: %d\n", e->extension_count);
  printf("checksum: 0x%x\n", e->checksum);
  if (edid_check_checksum(e)) puts("checksum valid");
}

bool edid_check_checksum(const edid_t *e) {
  uint8_t sum = 0;
  uint8_t *u8 = (uint8_t*)e;
  for (int i=0; i<128; i++) {
    sum += u8[i];
  }
  return sum == 0;
}
