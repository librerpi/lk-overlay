#include <app.h>
#include <dev/audio.h>
#include <assert.h>
#include <dev/gpio.h>
#include <kernel/event.h>
#include <lk/reg.h>
#include <math.h>
#include <platform/bcm28xx/clock.h>
#include <platform/bcm28xx/cm.h>
#include <platform/bcm28xx/dpi.h>
#include <platform/bcm28xx/hvs.h>
#include <platform/bcm28xx/otp.h>
#include <platform/bcm28xx/pll_read.h>
#include <platform/bcm28xx/print_timestamp.h>
#include <platform/bcm28xx/udelay.h>
#include <platform/interrupts.h>
#include <string.h>

#define PWM_BASE 0x7e20c000
#define PWM_CTL (PWM_BASE + 0x00)
#define PWM_CTL_PWEN1 0x01
#define PWM_CTL_MODE1 0x02
#define PWM_CTL_RPTL1 0x04
#define PWM_CTL_SBIT1 0x08
#define PWM_CTL_POLA1 0x10
#define PWM_CTL_USEF1 0x20
#define PWM_CTL_CLRF1 0x40
#define PWM_CTL_MSEN1 0x80

#define PWM_CTL_PWEN2 0x100
#define PWM_CTL_MODE2 0x200
#define PWM_CTL_RPTL2 0x400
#define PWM_CTL_USEF2 0x2000
#define PWM_CTL_MSEN2 0x8000

#define PWM_STA (PWM_BASE + 0x04)
#define PWM_STA_FULL1 0x01

#define PWM_DMAC (PWM_BASE + 0x08)

#define PWM_RNG1 (PWM_BASE + 0x10)
#define PWM_DAT1 (PWM_BASE + 0x14)

#define PWM_FIF1 (PWM_BASE + 0x18)

#define PWM_RNG2 (PWM_BASE + 0x20)
#define PWM_DAT2 (PWM_BASE + 0x24)

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

#define DMA0_BASE 0x7e007000
#define DMA_CS        0x00
#define DMA_CONBLK_AD 0x04
#define DMA_LEN       0x14

#define DMA_TI_INT_EN    BIT(0)
#define DMA_TI_WAIT_RESP BIT(3)
#define DMA_TI_DEST_DREQ BIT(6)
#define DMA_TI_SRC_INC   BIT(8)
#define DMA_TI_DREQ(n)   (n << 16)

#define LOGF(fmt, ...) { print_timestamp(); printf("[AUDIO:%s:%d]: "fmt, __FUNCTION__, __LINE__, ##__VA_ARGS__); }

extern const uint16_t wear[];
extern const uint16_t wear_end;
dma_cb dmacontrol0 __attribute__((aligned(32)));
dma_cb dmacontrol1 __attribute__((aligned(32)));

event_t buffer_middle = EVENT_INITIAL_VALUE(buffer_middle, true, EVENT_FLAG_AUTOUNSIGNAL);
event_t buffer_wrap = EVENT_INITIAL_VALUE(buffer_wrap, true, EVENT_FLAG_AUTOUNSIGNAL);

static const int buffer_size = 1024 * 4; // size in bytes
static uint32_t *buffer32;

static bool audio_init_done = false;

int write_ptr = 0;

static enum handler_return dma_interrupt_handler(void *arg) {
  enum handler_return ret = INT_NO_RESCHEDULE;
  //puts("dma irq");
  uint32_t cs = *REG32(DMA0_BASE+DMA_CS);
  //printf("cs: 0x%x\n", cs);
  if (cs & BIT(2)) {
    *REG32(DMA0_BASE+DMA_CS) = BIT(0) | BIT(2);
    uint32_t blk = *REG32(DMA0_BASE+DMA_CONBLK_AD);
    //printf("0x%x\n", blk);
    if (blk == (uint32_t)&dmacontrol0) {
      //printf("a");
      event_signal(&buffer_middle, false);
      ret = INT_RESCHEDULE;
    } else if (blk == (uint32_t)&dmacontrol1) {
      //printf("b");
      event_signal(&buffer_wrap, false);
      ret = INT_RESCHEDULE;
    }
  }
  uint32_t t = *REG32(PWM_STA);
  if (t & (BIT(4)|BIT(5))) {
    *REG32(PWM_STA) = BIT(4) | BIT(5);
    //printf("STA: 0x%x\n", t);
  }
  return ret;
}

static void audio_init(void) {
  if (audio_init_done) return;

  buffer32 = malloc(buffer_size);

  register_int_handler(16, dma_interrupt_handler, NULL);
  unmask_interrupt(16);
  LOGF("irq handler setup\n");

  audio_init_done = true;
}

#define BITS 8

static const int range = 1<<BITS;
static const int range_half = (1<<BITS)/2;

static void mux_analog_audio(void) {
  uint32_t revision = otp_read(30);
  int pin_left = 0;
  int pin_right = 0;

  if (revision & (1<<23)) { // new style revision
    uint32_t type = (revision >> 4) & 0xff;
    switch (type) {
    case 1: // pi 1b
    case 3: // pi 1b+
    case 4: // 2b
      pin_left = 45;
      pin_right = 40;
      break;
    case 8: // 3b
      pin_left = 41;
      pin_right = 40;
      break;
    default:
      LOGF("unhandled type code %d\n", type);
    }
  } else {
    switch (revision) {
    case 0xe: // pi 1b
      pin_left = 45;
      pin_right = 40;
      break;
    default:
      LOGF("unhandled type code %d\n", revision);
    }
  }
  if (pin_left) gpio_config(pin_left, 4);
  if (pin_right) gpio_config(pin_right, 4);
}

#define OVERSAMPLE 1

void audio_start(uint32_t samplerate, bool stereo) {
  if (!audio_init_done) audio_init();
  mux_analog_audio();

  if (stereo) {
    *REG32(PWM_CTL) = PWM_CTL_PWEN1 | PWM_CTL_USEF1 | PWM_CTL_MSEN1 |
        PWM_CTL_PWEN2 | PWM_CTL_USEF2 | PWM_CTL_MSEN2;
  } else {
    //*REG32(PWM_CTL) = PWM_CTL_PWEN1 | PWM_CTL_USEF1 |
    //    PWM_CTL_PWEN2 | PWM_CTL_USEF2;

    uint32_t t = PWM_CTL_PWEN1 | PWM_CTL_USEF1;
    t |= PWM_CTL_MSEN1;

    t = t | (t << 8);
    *REG32(PWM_CTL) = t;

    //*REG32(PWM_CTL) = PWM_CTL_PWEN1 | PWM_CTL_USEF1 | PWM_CTL_MSEN1;
    //*REG32(PWM_CTL) = PWM_CTL_PWEN2 | PWM_CTL_USEF2 | PWM_CTL_MSEN2;
  }
  *REG32(PWM_RNG1) = range;
  *REG32(PWM_RNG2) = range;
  *REG32(PWM_DMAC) = 5<<0 | 3<<8 | 1<<31;

  printf("samplerate: %d, range: %d\n", samplerate, range);
  clock_set_pwm(samplerate * range * OVERSAMPLE, PERI_PLLC_PER);

  int rate = measure_clock(24);
  printf("actual rate: %d, %d\n", rate, rate/OVERSAMPLE/range);

  memset(&dmacontrol0, 0, sizeof(dmacontrol0));
  dmacontrol0.source = 0xc0000000 | (uint32_t)buffer32;
  dmacontrol0.dest = PWM_FIF1;
  dmacontrol0.length = buffer_size/2;
  dmacontrol0.ti = DMA_TI_INT_EN | DMA_TI_DEST_DREQ | DMA_TI_SRC_INC | DMA_TI_DREQ(5) | (10 << 12);
  dmacontrol0.next_block = (uint32_t)&dmacontrol1;

  memset(&dmacontrol1, 0, sizeof(dmacontrol1));
  dmacontrol1.source = 0xc0000000 | (((uint32_t)buffer32) + (buffer_size/2));
  dmacontrol1.dest = PWM_FIF1;
  dmacontrol1.length = buffer_size/2;
  dmacontrol1.ti = DMA_TI_INT_EN | DMA_TI_DEST_DREQ | DMA_TI_SRC_INC | DMA_TI_DREQ(5) | (10 << 12);
  dmacontrol1.next_block = (uint32_t)&dmacontrol0;

  *REG32(DMA0_BASE+DMA_CONBLK_AD) = (uint32_t)&dmacontrol0;
  *REG32(DMA0_BASE+DMA_CS) = 1 | (8 << 16) | (15 << 20);

  puts("dma and pwm started");
  printf("buffer holds %d total samples\n", buffer_size/4);
  printf("%d L+R pairs\n", buffer_size/4/2);
  uint64_t uSec_per_wrap = (((uint64_t)buffer_size * 1000000) /4/2) / samplerate;
  printf("%d uSec per full wrap\n", (uint32_t)uSec_per_wrap);

  //for (int i=0; i<buffer_size/4; i++) {
  //  buffer32[i] = (i/2) & 0xff;
  //}
}

static bool right = false;
static uint32_t last_left, next_left;
static uint32_t last_right, next_right;

static uint32_t highest = 0;
static uint32_t lowest = 0xffffffff;

static void write_sample(uint32_t sample) {
  //printf("%d %d\n", write_ptr, sample);
  buffer32[write_ptr++] = sample;
  //uint32_t start = *REG32(ST_CLO);
  if (write_ptr == (buffer_size/4/2)) {
    //printf("slot %d, mid wait\n", write_ptr);
    event_wait(&buffer_wrap);
  }
  if (write_ptr == buffer_size/4) {
    //printf("slot %d, end wait\n", write_ptr);
    event_wait(&buffer_middle);
    write_ptr = 0;
  }
  //uint32_t end = *REG32(ST_CLO);
  //uint32_t delta = end - start;
  //if (delta > 20) printf("waited %d\n", delta);
}

uint32_t counts[257];

static void stats(uint32_t sample) {
  if (sample > highest) {
    printf("new high %d\n", sample);
    highest = sample;
  }
  if (sample < lowest) {
    printf("new low %d\n", sample);
    lowest = sample;
  }
  counts[sample]++;
}

void print_counts(void) {
  for (int i=0; i<257; i++) {
    printf("%d == %d\n", i, counts[i]);
  }
}

//static int toprint = 100;

// samples counts pairs of L+R
void audio_push_stereo(const int16_t *data, int samples) {
  if (!audio_init_done) audio_init();
  for (int i=0; i<(samples*2); i++) {
    //printf("i %d right: %d data: %d\n", i, right, data[i]);

    const uint32_t sample = (data[i] >> (16-BITS)) + range_half;
    //const uint16_t raw = (uint16_t)data[i];
    //const uint16_t flipped = raw ^ 0x8000;
    //const uint32_t sample = (flipped >> (16-BITS));
    //if ((data[i] > 3) || (data[i] < 0)) if (toprint-- > 0) printf("%d %d %d\n", raw, flipped, sample);
    stats(sample);
    //write_sample(sample);
    if (right) next_right = sample;
    else next_left = sample;

    right = !right;

    if (right) continue;
    //printf("%d %d\n", next_left, next_right);

    //buffer32[write_ptr++] = (last_left+next_left)>>1;
    //buffer32[write_ptr++] = (last_right+next_right)>>1;
    //buffer32[write_ptr++] = next_left;
    //buffer32[write_ptr++] = next_right;
    //write_sample((last_left+next_left)>>1);
    //write_sample((last_right+next_right)>>1);
    for (int j=0; j<OVERSAMPLE; j++) {
      write_sample(next_left);
      write_sample(next_right);
    }

    last_left = next_left;
    last_right = next_right;
    //buffer32[write_ptr++] = sample;
    //buffer32[write_ptr++] = sample;
    //buffer32[write_ptr++] = sample;
    //if (data[i] != 0) printf("sample %d\n", data[i] >> 8);
  }
}

void audio_push_mono_16bit(const int16_t *data, int samples) {
  if (!audio_init_done) audio_init();
  for (int i=0; i<(samples); i++) {
    const uint32_t sample = (data[i] >> (16-BITS)) + range_half;
    for (int j=0; j<OVERSAMPLE; j++) {
      write_sample(sample);
      write_sample(sample);
    }
    //if (data[i] != 0) printf("sample %d\n", data[i] >> 8);
  }
}

void audio_push_mono_8bit(const uint8_t *data, int samples) {
  for (int i=0; i<samples; i++) {
    for (int j=0; j<OVERSAMPLE; j++) {
      write_sample((data[i] >> 1) + 0x40);
      write_sample(0);
    }
  }
}

static void audio_entry(const struct app_descriptor *app, void* args) {
  int wear_bytes = &wear_end - wear;
  unsigned int wear_samples = wear_bytes / 2;


  //int rate = measure_clock(24);


  for (int i=0; i<32769; i++) counts[i] = 0;

  //unsigned int historgram[256];
  //for (int i=0; i<256; i++) historgram[i] = 0;

  for (unsigned int i=0; i<wear_samples; i++) {
    //buffer[i] = wear[i] >> 3;
    //historgram[buffer[i]]++;
    //buffer[i] = i % 54;
  }

  for (int i=0; i<256; i++) {
    //if (historgram[i] > 0) printf("%d: %d\n", i, historgram[i]);
  }


  return;
  while (true) {
    udelay(100000);
    printf("DMA_CS: 0x%x, DMA_LEN: 0x%x\n", *REG32(DMA0_BASE+DMA_CS), *REG32(DMA0_BASE+DMA_LEN));
    if (*REG32(DMA0_BASE+DMA_CS) & 2) break;
  }

  while (true) {
    while (*REG32(PWM_STA) & PWM_STA_FULL1) {}
    uint32_t t = *REG32(PWM_STA);
    if (t & ~0x302) printf("STA: 0x%x\n", t);
  }
}

//APP_START(audio)
//  .entry = audio_entry,
//APP_END
