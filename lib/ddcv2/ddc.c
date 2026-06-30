#include <dev/bcm-i2c.h>
#include <lib/edid.h>
#include <lib/hexdump.h>
#include <stdbool.h>
#include <stdio.h>
#include <lib/heap.h>

bool probe_ddcv2(struct pv_timings *prefered_timings) {
  int ret;
  uint8_t *buf = memalign(16, 128);

  buf[0] = 0;
  ret = smbus_write(2, 0x50, 1, buf, 1);
  if (ret != 0) {
    printf("i2c error on initial write\n");
    //return;
  }

  ret = smbus_read(2, 0x50, 0, buf, 128);
  if (ret != 0) return false;
  hexdump_ram(buf, 0, 128);

  edid_t *e = buf;
  if (!edid_check_checksum(e)) return false;

  edid_pretty_print(e);

  return edid_get_prefered(e, prefered_timings);
}
