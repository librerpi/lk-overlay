#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <platform/bcm28xx/dsi.h>
#include <platform/bcm28xx/udelay.h>

struct write_event { uint32_t reg, value; };
static struct write_event writes[512];
static unsigned int write_count, polls, scenario, delays_30ms;

uint32_t bcm_dsi1_read(uint32_t offset) {
  if (offset == BCM_DSI1_INT_STAT) {
    ++polls;
    if (scenario == 1) return 1u << 6; /* error */
    if (scenario == 2 && polls <= 500000) return 0; /* retry */
    return 1u << 1; /* complete */
  }
  return 0;
}
void bcm_dsi1_write(uint32_t offset, uint32_t value) {
  assert(write_count < sizeof(writes) / sizeof(writes[0]));
  writes[write_count++] = (struct write_event){offset, value};
}
void udelay(uint32_t usec) { if (usec == 30000) ++delays_30ms; }
static void reset(void) { write_count = polls = scenario = delays_30ms = 0; }
static void check(unsigned i, uint32_t reg, uint32_t value) {
  assert(i < write_count && writes[i].reg == reg && writes[i].value == value);
}
int main(void) {
  uint8_t data[20];
  for (unsigned i = 0; i < sizeof(data); ++i) data[i] = i;
  assert(bcm_dsi1_generic_write(NULL, 1) == -1);
  assert(bcm_dsi1_generic_write(data, 0) == -1);
  assert(bcm_dsi1_generic_write(data, 1025) == -1);

  reset(); assert(bcm_dsi1_generic_write(data, 1) == 0);
  check(2, BCM_DSI1_TXPKT1H, 0x13); check(3, BCM_DSI1_TXPKT1C, 0x409);
  reset(); assert(bcm_dsi1_generic_write(data, 2) == 0);
  check(2, BCM_DSI1_TXPKT1H, 0x00010023);

  reset(); assert(bcm_dsi1_generic_write(data, 6) == 0);
  for (unsigned i = 0; i < 6; ++i) check(i, BCM_DSI1_TXPKT_CMD_FIFO, i);
  check(8, BCM_DSI1_TXPKT1H, 0x06000629);
  check(9, BCM_DSI1_TXPKT1C, 0x40d);

  reset(); assert(bcm_dsi1_generic_write(data, 20) == 0);
  check(0, BCM_DSI1_TXPKT_PIX_FIFO, 0x03020100);
  check(4, BCM_DSI1_TXPKT_PIX_FIFO, 0x13121110);
  check(7, BCM_DSI1_TXPKT1H, 0x00001429);
  check(8, BCM_DSI1_TXPKT1C, 0x60d);

  reset(); scenario = 1;
  assert(bcm_dsi1_generic_write(data, 1) == -1);
  check(4, BCM_DSI1_TXPKT1C, 0); /* reset clears CMD_EN */
  reset(); scenario = 2;
  assert(bcm_dsi1_generic_write(data, 1) == 0 && delays_30ms == 2);
  puts("DSI packet tests passed");
  return 0;
}
