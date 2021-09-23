#pragma once

typedef struct {
  uint32_t magic;
  uint32_t header_size;
  uint32_t dtb_base;
  uint32_t mmio_base;
  uint32_t end_of_ram;
} inter_core_header;

#define INTER_ARCH_MAGIC 0xa8ca6706

extern uint32_t fb_addr;
extern uint32_t w, h;
extern uint32_t stage2_arch_init, stage2_arm_start;
