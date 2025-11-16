#include <lib/elf.h>
#include <stdio.h>
#include <string.h>
#include <usbhooks.h>

#include "stage1.h"

static void stage1_msd_probed(const char *name) {
  char buffer[64];
  for (int i=0; i<4; i++) {
    snprintf(buffer, 64, "%sp%d", name, i);
    add_boot_target(strdup(buffer));
  }
}

USB_HOOK_START(stage1)
  //.init = bad_apple_usb_init,
  .msd_probed = stage1_msd_probed,
USB_HOOK_END
