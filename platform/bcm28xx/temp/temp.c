#include <app.h>
#include <kernel/timer.h>
#include <lk/console_cmd.h>
#include <lk/reg.h>
#include <platform/bcm28xx/cm.h>
#include <platform/bcm28xx/otp.h>
#include <stdio.h>

#define CM_TSENSCTL   0x7e1010e0
#define CM_TSENSCTL_ENAB_SET 0x00000010
#define CM_TSENSCTL_ENAB_CLR 0xffffffef
#define CM_TSENSCTL_BUSY_SET 0x00000080
#define CM_TSENSDIV   0x7e1010e4
#define TS_TSENSCTL   0x7e212000
#define TS_TSENSSTAT  0x7e212004

static int cmd_show_temp(int argc, const console_cmd_args *argv);
static void setup_tsens(void);
bool tsens_setup = false;
timer_t poller;
float increment, offset;

STATIC_COMMAND_START
STATIC_COMMAND("show_temp", "print internal temp sensor", &cmd_show_temp)
STATIC_COMMAND_END(temp);

typedef struct {
  float increment;
  float offset;
} coefficients_t;

coefficients_t coefficients[] = {
  [0] = { // bcm2835
    .increment = 0.538,
    .offset = 407
  },
  [1] = { // bcm2836
    .increment = 0.538,
    .offset = 407
  },
  [2] = { // bcm2837
    .increment = 0.538,
    .offset = 412
  },
  [3] = { // bcm2711
    .increment = 0.487,
    .offset = 410.04
  },
};

static uint32_t get_raw_temp(void) {
  if (!tsens_setup) setup_tsens();
  return *REG32(TS_TSENSSTAT);
}

static float convert_temp(uint32_t raw) {
  float converted = offset - (raw & 0x3ff) * increment;
  return converted;
}

static float get_converted_temp(void) {
  uint32_t raw = get_raw_temp();
  //printf("raw %d\n", raw);
  return convert_temp(raw);
}

static void setup_tsens() {
  *REG32(CM_TSENSCTL) = (*REG32(CM_TSENSCTL) & CM_TSENSCTL_ENAB_CLR) | CM_PASSWORD; // disable TSENS
  while (*REG32(CM_TSENSCTL) & CM_TSENSCTL_BUSY_SET) {} // wait for it to stop
  *REG32(CM_TSENSCTL) = CM_PASSWORD; // clear all config
  *REG32(CM_TSENSDIV) = CM_PASSWORD | 0x5000; // set divisor
  *REG32(CM_TSENSCTL) = CM_PASSWORD | 1; // set clock source
  *REG32(CM_TSENSCTL) = CM_PASSWORD | CM_TSENSCTL_ENAB_SET | 1; // enable

  *REG32(TS_TSENSCTL) = 0x4380004;
  *REG32(TS_TSENSCTL) |= 2;

  uint32_t revision = otp_read(30);
  uint32_t proc = (revision >> 12) & 0xf;
  increment = coefficients[proc].increment;
  offset = coefficients[proc].offset;

  tsens_setup = true;
}

static int cmd_show_temp(int argc, const console_cmd_args *argv) {
  double converted = get_converted_temp();
  uint32_t raw = get_raw_temp();
  printf("Temp: %f\nRaw: %d\n", converted, raw);
  return 0;
}

static int32_t abs32(int32_t a) {
  if (a < 0) return a * -1;
  else return a;
}

static enum handler_return poller_entry(struct timer *t, lk_time_t now, void *arg) {
  static int32_t last_temp = 0;
  int32_t temp = get_raw_temp();
  uint32_t diff = abs32(temp - last_temp);
  if (diff > 3) {
    printf("temp changed %f -> %f\n", (double)convert_temp(last_temp), (double)convert_temp(temp));
    last_temp = temp;
  }
  return INT_NO_RESCHEDULE;
}

static void temp_init(const struct app_descriptor *app) {
  timer_initialize(&poller);
  timer_set_periodic(&poller, 1000, poller_entry, NULL);
}

APP_START(temp)
  .init = temp_init,
APP_END
