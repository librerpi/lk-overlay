#include <lk/console_cmd.h>
#include <lk/reg.h>
#include <platform/bcm28xx/clock.h>
#include <platform/bcm28xx/dma.h>
#include <stdio.h>
#include <stdlib.h>
#include <strings.h>

static dma_cb cb0 __attribute__((aligned(32)));

static void do_bench(int shift, int size) {
  uint8_t *source_buffer = malloc(size);
  uint8_t *dest_buffer = malloc(size);

  bzero(&cb0, sizeof(cb0));

  cb0.ti = DMA_TI_DEST_INC | DMA_TI_SRC_INC | DMA_TI_BURST(16) | DMA_TI_DEST_WIDE | DMA_TI_SRC_WIDE;
  cb0.source = (uint32_t)source_buffer;
  cb0.dest = (uint32_t)dest_buffer;
  cb0.length = size;

  dma_controller *chan = get_dma(0);
  chan->conblk_ad = (uint32_t)&cb0;

  uint32_t start = *REG32(ST_CLO);
  chan->cs = 1 | (8 << 16) | (15 << 20);

  while (chan->cs & 1) {}

  uint32_t stop = *REG32(ST_CLO);
  uint32_t delta = stop - start;
  if (delta > 0) {
    //printf("%d bytes in %d uSec, %d MBit/s\n", size, delta, (size*8)/delta);
    printf("%d %d\n", shift, (size*8)/delta);
  }

  free(source_buffer);
  free(dest_buffer);
}

static int cmd_dma_bench(int argc, const console_cmd_args *argv) {
  for (int j=0; j<20; j++) {
    for (int i=0; i<21; i++) {
      int size = 1<<i;
      do_bench(i, size);
    }
  }

  return 0;
}

STATIC_COMMAND_START
STATIC_COMMAND("dma_bench", "dma benchmark", &cmd_dma_bench)
STATIC_COMMAND_END(dma);

