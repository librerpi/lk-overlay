#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <dev/gpio.h>
#include <platform/bcm28xx/gpio.h>
#include <platform/bcm28xx/panel.h>
#include <platform/bcm28xx/i2c.h>
#include <platform/bcm28xx/udelay.h>

struct event { char type; unsigned int a, b; };
static struct event events[512];
static unsigned int count, transfers, fail_at, ready;
static void event(char type, unsigned int a, unsigned int b) {
  assert(count < sizeof(events) / sizeof(events[0]));
  events[count++] = (struct event){type, a, b};
}
int gpio_config(unsigned int pin, unsigned int setting) {
  event('g', pin, setting);
  return 0;
}
void i2c_set_rate(unsigned int bus, unsigned long rate) { event('r', bus, rate); }
void udelay(uint32_t usec) { event('d', usec, 0); }
int i2c_xfer(unsigned int bus, unsigned int addr, char *send, size_t send_len,
             char *recv, size_t recv_len) {
  assert(bus == 0 && addr == 0x45 && (send_len == 1 || send_len == 2));
  event(recv_len ? 'R' : 'W', (uint8_t)send[0], send_len == 2 ? (uint8_t)send[1] : 0);
  if (++transfers == fail_at) return -1;
  if (recv_len) { assert(recv_len == 1); *recv = (char)ready; }
  return 0;
}
static void clear(void) { count = transfers = fail_at = 0; ready = 1; }
static void check(unsigned int index, char type, unsigned int a, unsigned int b) {
  assert(index < count && events[index].type == type && events[index].a == a && events[index].b == b);
}
int main(void) {
  clear();
  assert(bcm_panel_attiny_init() == 0 && count == 6);
  check(0, 'g', 44, 5); check(1, 'g', 45, 5); check(2, 'r', 0, 100000);
  check(3, 'R', 0x80, 0); check(4, 'W', 0x85, 0); check(5, 'W', 0x86, 0);

  clear();
  assert(bcm_panel_attiny_power(1) == 0 && count == 8);
  check(0, 'W', 0x83, 0); check(1, 'd', 5000, 0); check(2, 'W', 0x81, 4);
  check(3, 'd', 5000, 0); check(4, 'W', 0x82, 128); check(5, 'd', 5000, 0);
  check(6, 'W', 0x83, 1); check(7, 'd', 80000, 0);

  clear();
  assert(bcm_panel_attiny_power(0) == 0 && count == 6);
  check(0, 'W', 0x86, 0); check(1, 'd', 5000, 0); check(2, 'W', 0x81, 0);
  check(3, 'W', 0x82, 2); check(4, 'W', 0x83, 0); check(5, 'd', 30000, 0);

  clear();
  assert(bcm_panel_attiny_bridge_power_on() == 0 && count == 4);
  check(0, 'd', 100000, 0); check(1, 'W', 0x85, 1);
  check(2, 'd', 25000, 0); check(3, 'R', 0x82, 0);

  clear(); ready = 0;
  assert(bcm_panel_attiny_bridge_power_on() == 0 && transfers == 101 && count == 203);

  clear();
  assert(bcm_panel_attiny_set_bridge_reset(1) == 0 && count == 9);
  check(0, 'W', 0x83, 13); check(1, 'd', 5000, 0); check(2, 'W', 0x8d, 4);
  check(3, 'd', 5000, 0); check(4, 'W', 0x8c, 0x7c); check(5, 'd', 5000, 0);
  check(6, 'W', 0x90, 0); check(7, 'W', 0x91, 0); check(8, 'd', 100000, 0);

  clear(); fail_at = 2;
  assert(bcm_panel_attiny_power(1) == -1 && count == 3);
  clear(); fail_at = 1;
  assert(bcm_panel_attiny_init() == -1 && count == 4);
  clear();
  assert(bcm_panel_attiny_set_backlight(255) == 0 && count == 1);
  check(0, 'W', 0x86, 255);
  puts("panel sequence tests passed");
  return 0;
}
