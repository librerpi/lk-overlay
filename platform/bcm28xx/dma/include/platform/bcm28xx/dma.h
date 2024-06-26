#pragma once

#include <stdint.h>

#include <platform/bcm28xx.h>

#define BIT(b) (1LL << b)

#define DMA_TI_INT_EN       BIT(0)
#define DMA_TI_WAIT_RESP    BIT(3)
#define DMA_TI_DEST_INC     BIT(4)
// 32bit when false, 128bit when true
#define DMA_TI_DEST_WIDE    BIT(5)
#define DMA_TI_DEST_DREQ    BIT(6)
#define DMA_TI_DEST_IGNORE  BIT(7)
#define DMA_TI_SRC_INC      BIT(8)
// 32/128 again
#define DMA_TI_SRC_WIDE     BIT(9)
#define DMA_TI_SRC_DREQ     BIT(10)
#define DMA_TI_SRC_IGNORE   BIT(11)
#define DMA_TI_BURST(n)     (( (n - 1) & 0xf) << 12)
#define DMA_TI_DREQ(n)      ((n & 0x1f) << 16)
#define DMA_TI_WAITS(n)     ((n & 0x1f) << 21)

typedef struct {
  uint32_t ti;
  uint32_t source;
  uint32_t dest;
  uint32_t length;
  uint32_t stride;
  uint32_t next_block;
  uint32_t pad1;
  uint32_t pad2;
} dma_cb;

typedef struct {
  volatile uint32_t cs;
  volatile uint32_t conblk_ad;
  volatile uint32_t ti;
  volatile uint32_t source_ad;
} dma_controller;

static dma_controller *get_dma(int channel) {
  uint32_t base = 0;
  switch (channel) {
  case 0:
  case 1:
  case 2:
  case 3:
  case 4:
  case 5:
  case 6:
  case 7:
  case 8:
  case 9:
  case 10:
  case 11:
  case 12:
  case 13:
  case 14:
    base = (DMA_BASE) + (channel * 0x100);
    break;
  }
  return (dma_controller*)base;
}
