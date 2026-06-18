#include <stdio.h>
#include <cksum-helper/cksum-helper.h>
#include <dev/spi.h>
#include <lib/hexdump.h>
#include <arch.h>
#include <lib/heap.h>

#include "stage1.h"

extern uint32_t stage2_offset;
extern uint32_t stage2_length;

void try_to_spi_boot(void) {
  if ((stage2_offset == 0) || (stage2_length == 0)) {
    printf("stage2 not found in SPI\n");
    return;
  }
  void *buffer = memalign(16, stage2_length);
  spi_flash_read_data(buffer, stage2_offset, stage2_length);

  uint8_t hash[sha256_implementation.hash_size];
  hash_blob(&sha256_implementation, buffer, stage2_length, hash);
  print_hash(hash, sha256_implementation.hash_size);

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
