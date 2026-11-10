#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <lk/reg.h>
#include <platform/bcm28xx.h>
#include <platform/bcm28xx/a2w.h>
#include <platform/bcm28xx/arm_chainloader.h>
#include <platform/bcm28xx/cm.h>

struct reg { uint32_t address, value; };
static struct reg regs[64], trace[128];
static unsigned int count, trace_count;
static struct reg *last;
static struct reg *lookup(uint32_t address) {
  for (unsigned int i = 0; i < count; ++i)
    if (regs[i].address == address) return &regs[i];
  assert(count < 64);
  regs[count].address = address;
  return &regs[count++];
}
volatile uint32_t *test_reg(uint32_t address) {
  if (last) { assert(trace_count < 128); trace[trace_count++] = *last; }
  last = lookup(address);
  return &last->value;
}
int main(void) {
  static const uint32_t pllb_sequence[] = {
    0x5a000300, 0x5a000302, 0x5a0000aa,
    0x5a000303, 0x5a000302, 0x5a000000,
  };
  lookup(A2W_PLLB_DIG0)->value = 0x00110011;
  lookup(A2W_PLLB_DIG1)->value = 0x00224022;
  lookup(A2W_PLLB_DIG2)->value = 0x01334433;
  lookup(A2W_PLLB_DIG3)->value = 0x00440044;
  assert(arm_chainloader_setup_clock(0) == -1 && trace_count == 0);
  assert(arm_chainloader_setup_clock(1000) == 0);
  test_reg(0); /* snapshot the final write */
  assert(lookup(A2W_XOSC_CTRL)->value == (A2W_PASSWORD | A2W_XOSC_CTRL_PLLBEN_SET));
  assert(lookup(A2W_PLLB_FRAC)->value == (A2W_PASSWORD | 0x15555));
  assert(lookup(A2W_PLLB_CTRL)->value == (A2W_PASSWORD | 0x21034));
  assert(lookup(A2W_PLLB_ARM)->value == (A2W_PASSWORD | 4));
  assert(lookup(CM_ARMCTL)->value == (CM_PASSWORD | 4 | CM_ARMCTL_ENAB_SET));
  assert(lookup(A2W_PLLB_DIG0)->value == (A2W_PASSWORD | 0x00110011));
  assert(lookup(A2W_PLLB_DIG1)->value == (A2W_PASSWORD | 0x00224022));
  assert(lookup(A2W_PLLB_DIG2)->value == (A2W_PASSWORD | 0x01334433));
  assert(lookup(A2W_PLLB_DIG3)->value == (A2W_PASSWORD | 0x00440044 | 0x42));
  unsigned int found = 0;
  for (unsigned int i = 0; i < trace_count; ++i) {
    if (trace[i].address == CM_PLLB && found < sizeof(pllb_sequence) / sizeof(pllb_sequence[0])) {
      assert(trace[i].value == pllb_sequence[found]);
      ++found;
    }
  }
  assert(found == sizeof(pllb_sequence) / sizeof(pllb_sequence[0]));
  puts("ARM PLLB clock sequence tests passed");
  return 0;
}
