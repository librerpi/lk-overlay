#include <lib/partition.h>
#include <lk/console_cmd.h>
#include <platform/bcm28xx/sdhost_impl.h>
#include <lk/reg.h>
#include <platform/bcm28xx/clock.h>
#include <stdlib.h>
#include <platform/bcm28xx/pll.h>

static int cmd_sdhost_init(int argc, const console_cmd_args *argv);
static int cmd_sdhost_div(int argc, const console_cmd_args *argv);
static int cmd_sdhost_bench(int argc, const console_cmd_args *argv);

STATIC_COMMAND_START
STATIC_COMMAND("sdhost_init", "initialize the sdhost driver", &cmd_sdhost_init)
STATIC_COMMAND("sdhost_div", "set sdhost clock div", &cmd_sdhost_div)
STATIC_COMMAND("sdhost_bench", "benchmark sd card", &cmd_sdhost_bench)
STATIC_COMMAND_END(sdhost);

static int cmd_sdhost_init(int argc, const console_cmd_args *argv) {
  rpi_sdhost_init();
  partition_publish("sdhost", 0);
  return 0;
}

static int cmd_sdhost_div(int argc, const console_cmd_args *argv) {
  if (argc != 2) {
    printf("usage: sdhost_div <divisor>\n");
    return 0;
  }
  rpi_sdhost_set_clock(argv[1].u);
  return 0;
}

static int cmd_sdhost_bench(int argc, const console_cmd_args *argv) {
  bdev_t *dev = bio_open("sdhost");
  if (!dev) {
    printf("error opening block device\n");
    return -1;
  }
  uint8_t *buf = malloc(1024*1024);
  for (int i=3; i<=70; i++) {
    rpi_sdhost_set_clock(i);
    uint32_t start = *REG32(ST_CLO);
    bio_read_block(dev, buf, 0, (1024*1024)/512);
    uint32_t stop = *REG32(ST_CLO);
    uint32_t interval = stop - start;
    float bits = 1024*1024*8;
    float delta = interval;
    double mbit = bits/delta;
    printf("%f MHz, \t", ((double)vpu_clock)/i);
    printf("%d uSec to read 1MB\t", interval);
    printf("%f mbits/sec\n", mbit);
  }
  free(buf);
  bio_close(dev);
  return 0;
}
