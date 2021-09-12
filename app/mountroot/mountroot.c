#include <app.h>
#include <lib/fs.h>
#include <lib/partition.h>
#include <lk/init.h>
#include <platform/bcm28xx/sdhost_impl.h>
#include <stdio.h>

static void mountroot_entry(uint level) {
  int ret;
  puts("mountroot entry\n");

  bdev_t *sd = rpi_sdhost_init();
  partition_publish("sdhost", 0);

  ret = fs_mount("/root", "ext2", "sdhostp1");
  if (ret) {
    printf("mount failure: %d\n", ret);
    return;
  }
}

LK_INIT_HOOK(mountroot, &mountroot_entry, LK_INIT_LEVEL_TARGET);
