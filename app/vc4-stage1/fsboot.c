#include <stdint.h>
#include <lib/fs.h>
#include <lib/elf.h>
#include <platform/bcm28xx/print_timestamp.h>
#include <stdio.h>
#include <arch.h>
#include <stdlib.h>

#include "stage1.h"

#define logf(fmt, ...) { print_timestamp(); printf("[stage1:%s:%d]: "fmt, __FUNCTION__, __LINE__, ##__VA_ARGS__); }

static ssize_t fs_read_wrapper(struct elf_handle *handle, void *buf, uint64_t offset, size_t len) {
  return fs_read_file(handle->read_hook_arg, buf, offset, len);
}

void try_sd_boot(const char *device) {
  int ret;
  logf("trying to boot from %s\n", device);
  ret = fs_mount("/root", "ext2", device);
  if (ret) {
    printf("mount failure: %d\n", ret);
    return;
  }
  filehandle *stage2;
  ret = fs_open_file("/root/boot/lk.elf", &stage2);
  if (ret) {
    printf("failed to open /root/boot/lk.elf: %d\n", ret);
    goto unmount;
  }

  elf_handle_t *stage2_elf = malloc(sizeof(elf_handle_t));
  ret = elf_open_handle(stage2_elf, fs_read_wrapper, stage2, false);
  if (ret) {
    printf("failed to elf open: %d\n", ret);
    goto closefile;
  }
  void *entry = load_and_run_elf(stage2_elf);
  fs_close_file(stage2);
  arch_chain_load(entry, 0, 0, 0, 0);
  return;
  closefile:
    fs_close_file(stage2);
  unmount:
    fs_unmount("/root");
}
