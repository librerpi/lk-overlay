#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <platform/bcm28xx/bridge.h>
#include <platform/bcm28xx/dsi.h>
#include <platform/bcm28xx/panel.h>
#include <platform/bcm28xx/udelay.h>

struct event { char kind; uint32_t a, b; };
static struct event events[32];
static unsigned int count, writes, fail_write, fail_panel;
static void record(char kind, uint32_t a, uint32_t b) {
  assert(count < sizeof(events) / sizeof(events[0]));
  events[count++] = (struct event){kind, a, b};
}
int bcm_panel_attiny_init(void) { record('p', 0, 0); return fail_panel == 1 ? -1 : 0; }
int bcm_panel_attiny_power(int on) { record('p', 1, on); return fail_panel == 2 ? -1 : 0; }
int bcm_panel_attiny_bridge_power_on(void) { record('p', 2, 0); return fail_panel == 3 ? -1 : 0; }
int bcm_panel_attiny_set_bridge_reset(int release) { record('p', 3, release); return fail_panel == 4 ? -1 : 0; }
void udelay(uint32_t usec) { record('d', usec, 0); }
int bcm_dsi1_generic_write(const void *data, size_t len) {
  const uint8_t *p = data;
  assert(len == 6);
  record('w', p[0] | ((uint32_t)p[1] << 8),
         p[2] | ((uint32_t)p[3] << 8) | ((uint32_t)p[4] << 16) | ((uint32_t)p[5] << 24));
  return ++writes == fail_write ? -1 : 0;
}
static void check(unsigned i, char kind, uint32_t a, uint32_t b) {
  assert(i < count && events[i].kind == kind && events[i].a == a && events[i].b == b);
}
int main(void) {
  static const struct event expected[] = {
    {'p', 0, 0}, {'p', 1, 1}, {'p', 2, 0}, {'p', 3, 1},
    {'w', 0x0210, 3}, {'w', 0x0164, 5}, {'w', 0x0168, 5},
    {'w', 0x0144, 0}, {'w', 0x0148, 0}, {'w', 0x0114, 3}, {'w', 0x0450, 0},
    {'w', 0x0420, 0x00100150}, {'w', 0x0464, 0x040f},
    {'w', 0x0424, 2 | (46u << 16)}, {'w', 0x0428, 800 | (1u << 16)},
    {'w', 0x042c, 2 | (21u << 16)}, {'w', 0x0430, 480 | (7u << 16)},
    {'d', 100000, 0}, {'w', 0x0104, 1}, {'w', 0x0204, 1}, {'d', 100000, 0},
  };
  assert(bcm_tc358762_init() == 0 && count == sizeof(expected) / sizeof(expected[0]));
  for (unsigned i = 0; i < count; ++i) check(i, expected[i].kind, expected[i].a, expected[i].b);
  count = writes = 0; fail_write = 3;
  assert(bcm_tc358762_init() == -1 && count == 7 && writes == 3);
  count = writes = fail_write = 0; fail_panel = 2;
  assert(bcm_tc358762_init() == -1 && count == 2 && writes == 0);
  puts("bridge sequence tests passed");
  return 0;
}
