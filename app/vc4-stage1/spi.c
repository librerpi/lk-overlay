#include <stdio.h>
#include <cksum-helper/cksum-helper.h>
#include <dev/spi.h>
#include <lib/hexdump.h>
#include <arch.h>
#include <lib/heap.h>

#include "stage1.h"

void try_to_spi_boot(void) {
  uint8_t *buffer;
  ssize_t stage2_length = spi_read_file("stage2.elf", &buffer);
  printf("stage2 is %ld bytes long and is now at %p\n", stage2_length, buffer);

  if (stage2_length <= 0) {
    printf("error reading spi file, %ld\n", stage2_length);
    return;
  }

  //hexdump_ram(buffer, 0, stage2_length);

  elf_handle_t *stage2_elf = malloc(sizeof(elf_handle_t));
  int ret = elf_open_handle_memory(stage2_elf, buffer, stage2_length);
  if (ret) {
    printf("failed to elf open: %d\n", ret);
    return;
  }
  void *entry = load_and_run_elf(stage2_elf);
  free(buffer);
  if (true) {
    arch_chain_load(entry, 0, 0, 0, 0);
  }
}
