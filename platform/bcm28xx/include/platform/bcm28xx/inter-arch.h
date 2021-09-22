#pragma once

typedef struct {
  uint32_t magic;
  uint32_t header_size;
  uint32_t dtb_base;
  uint32_t mmio_base;
  uint32_t end_of_ram;
} inter_core_header;

#define INTER_ARCH_MAGIC 0xa8ca6706
