#include <lib/edid.h>
#include <lib/hexdump.h>
#include <dev/bcm-i2c.h>
#include <stdbool.h>
#include <lib/heap.h>

bool probe_ddcv2() {
  int ret;
  uint8_t *buf = memalign(16, 128);

  buf[0] = 0;
  ret = smbus_write(2, 0x50, 1, buf, 1);
  if (ret != 0) {
    //printf("i2c error on initial write, giving up\n");
    //return;
  }

  ret = smbus_read(2, 0x50, 0, buf, 128);
  if (ret != 0) return;
  hexdump_ram(buf, 0, 128);

  edid_t *e = buf;
  edid_pretty_print(e);
  return false;
}
