#include <app.h>
#include <lk/console_cmd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include "filter1.h"

extern int16_t pink_start[];

int32_t do_fir(uint32_t rep, int16_t *coef, uint32_t stride, int16_t *input);

static int cmd_matrix_dump(int argc, const console_cmd_args *argv) {
  int16_t *t = malloc(2 * 16 * 64);
  asm volatile ("v16st HX(0++, 0), (%0+=%1) REP64": : "r"(t), "r"(16*2));
  for (int row=0; row<16; row++) {
    for (int col=0; col<16; col++) {
      printf("%10d", t[(row*16)+col]);
    }
    printf("\n");
  }
  free(t);
  return 0;
}

STATIC_COMMAND_START
STATIC_COMMAND("matrix_dump", "dump matrix", &cmd_matrix_dump)
STATIC_COMMAND_END(fir);

static void fir_entry(const struct app_descriptor *app, void *args) {
  // REP count MUST be in r0

  int size = 2 * (24000 + (SAMPLEFILTER1_TAP_NUM*2));
  int16_t *tempbuf = malloc(size);
  bzero(tempbuf, size);

  memcpy(tempbuf + SAMPLEFILTER1_TAP_NUM, pink_start, 24000 * 2);

  for (int i=0; i<10; i++) {
    uint32_t t = do_fir(11, filter1_taps, 2*16, tempbuf + i);
    printf("sum %d\n", t);
  }
}

APP_START(fir)
  .entry = fir_entry
APP_END
