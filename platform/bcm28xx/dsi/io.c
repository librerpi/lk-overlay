#include <stdint.h>
#include <lk/reg.h>
#include <platform/bcm28xx.h>
#include <platform/bcm28xx/dma.h>
#include <platform/bcm28xx/dsi.h>
#include <stdio.h>

#define DSI1_BASE (BCM_PERIPH_BASE_VIRT + 0x700000)

static volatile uint32_t dma_word __attribute__((aligned(4)));
static dma_cb dma_control_block __attribute__((aligned(32)));

uint32_t bcm_dsi1_read(uint32_t offset) {
  return *REG32(DSI1_BASE + offset);
}

void bcm_dsi1_write(uint32_t offset, uint32_t value) {
  dma_controller *chan = get_dma(0);
  unsigned int timeout = 100000;
  dma_word = value;
  dma_control_block.ti = DMA_TI_WAIT_RESP;
  dma_control_block.source = (uint32_t)&dma_word;
  dma_control_block.dest = DSI1_BASE + offset;
  dma_control_block.length = sizeof(dma_word);
  dma_control_block.stride = 0;
  dma_control_block.next_block = 0;
  dma_control_block.pad1 = 0;
  dma_control_block.pad2 = 0;
  chan->conblk_ad = (uint32_t)&dma_control_block;
  chan->cs = 1u | (8u << 16) | (15u << 20);
  while ((chan->cs & 1u) && timeout--)
    ;
  if (!timeout)
    puts("DSI: DMA register write timeout");
}
