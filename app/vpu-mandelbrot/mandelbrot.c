#include <app.h>
#include <lib/font.h>
#include <lk/reg.h>
#include <math.h>
#include <platform/bcm28xx.h>
#include <platform/bcm28xx/hvs.h>
#include <platform/bcm28xx/pll_read.h>
#include <platform/time.h>
#include <stdlib.h>

uint32_t mandel_asm(void *buffer, uint32_t ustart, uint32_t vstart, uint32_t delta, uint32_t width, uint32_t height);

static uint32_t vpu_clock;

#define SHIFT (22u)
#define MUL (1 << SHIFT)

void __attribute__(( optimize("-O0"))) dump_entire_matrix(void) {
  uint32_t matrix[64][16];
  asm volatile ("v32st HY(0++,0), (%0+=%1) REP64"::"r"(matrix), "r"(4*16));
  for (int row = 0; row < 64; row++) {
    printf("%d: ", row);
    for (int col = 0; col < 16; col++) {
      if (row != 7) {
        printf("%f ", (double)matrix[row][col] / ((double)(2^22)));
      } else {
        printf("%d ", matrix[row][col]);
      }
    }
    puts("");
  }
}

void do_mandel_render(gfx_surface *image, uint32_t ustart, uint32_t vstart, uint32_t delta, uint32_t width, uint32_t height) {
  volatile uint32_t *data = image->ptr;

  for (int j = 0; j < 16; j++) { data[j] = j*delta; }

  // zero DDR2 perf counters
  *REG32(SD_IDL) = 0;

  uint32_t internal_measured = mandel_asm(image->ptr, ustart, vstart, delta, width, height);
  uint64_t idle = *REG32(SD_IDL);
  uint64_t total = *REG32(SD_CYC);

  if (true) {
    //dump_entire_matrix();
    char buffer[128];
    double ms_spent = (double)internal_measured / 1000;
    snprintf(buffer, 127, "mandelbrot took %f ms, %f fps, @ %d MHz\n", ms_spent, 1000 / ms_spent, vpu_clock);;
    const char *c;
    int x=0;
    for (c = buffer; *c; c++) {
      font_draw_char(image, *c, x, 0, 0xffffffff);
      x += FONT_X;
    }
    snprintf(buffer, 127, "mandelbrot took %lldk cycles of the ddr2 clock, %lldk of which where idle, %lldk spent\n", total/1000, idle/1000, (total-idle)/1000);
    x = 0;
    for (c = buffer; *c; c++) {
      font_draw_char(image, *c, x, FONT_Y, 0xffffffff);
      x += FONT_X;
    }
  }
}

static void mandelbrot_entry(const struct app_descriptor *app, void *args) {
  gfx_surface *imagea, *imageb;
  hvs_layer *layer;
  const int channel = PRIMARY_HVS_CHANNEL;
  int vpu = measure_clock(5);
  vpu_clock = vpu / 1000 / 1000;

  const int width = 640;
  const int height = 480;

  imagea = gfx_create_surface(NULL, width, height, width, GFX_FORMAT_RGB_x888);
  imageb = gfx_create_surface(NULL, width, height, width, GFX_FORMAT_RGB_x888);
  imagea->flush = 0;
  imageb->flush = 0;

  layer = malloc(sizeof(hvs_layer));
  mk_unity_layer(layer, imagea, 40, 0, 0);
  layer->w = 1280;
  layer->h = 1024;

  mutex_acquire(&channels[channel].lock);
  hvs_dlist_add(channel, layer);
  hvs_update_dlist(channel);
  mutex_release(&channels[channel].lock);

  double step = 0;
  bool whichframe = false;

  while (true) {
    hvs_wait_vsync(channel);
    double d = 3.*pow(0.1,1+cos(.2*step));
    int ustart = (-.745-.5*d)*MUL;
    int vstart = (.186-.5*d)*MUL;
    int delta = d/480.0*MUL;
    layer->fb = (whichframe == 0) ? imagea : imageb;
    whichframe = !whichframe;

    do_mandel_render(layer->fb, ustart, vstart, delta, width, height);

    mutex_acquire(&channels[channel].lock);
    hvs_update_dlist(channel);
    mutex_release(&channels[channel].lock);

    step += 0.1;

    //thread_sleep(10 * 1000);
  }
}

APP_START(vpu_mandelbrot)
  .entry = mandelbrot_entry
APP_END
