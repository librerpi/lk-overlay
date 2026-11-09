#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <lk/reg.h>
#include <platform/bcm28xx/arm.h>
#include <platform/bcm28xx/arm_chainloader.h>
#include <platform/bcm28xx/power.h>
#include <platform/bcm28xx/udelay.h>

static const uint8_t expected[] = {
  0x10,0x14,0x10,0x14,0x10,0x14,0x10,0x14,0x10,0x1c,0x18,0x1c,0x18,0,
  0x10,0x14,0x10,0x1c,0x10,0x14,0x10,0x14,0x10,0x14,0x10,0x14,0x10,0,
  0x10,0x14,0x10,0x1c,0x18,0x1c,0x10,0x14,0x18,0x1c,0x10,0x14,0x10,0,
  0x10,0x14,0x18,0x14,0x10,0x14,0x10,0x14,0x10,0x14,0x10,0x14,0x10,0,
  0x10,0x14,0x18,0x14,0x18,0x14,0x10,0x14,0x10,0x14,0x10,0x14,0x18,0,
};
static uint32_t control0 = 0xa5000008u, control1 = ARM_C1_REQSTOP | ARM_C1_PERSON, proc;
static uint32_t expected_control0 = 0xa5000008u;
static unsigned int count30, count300;
volatile uint32_t *test_reg(uint32_t address) {
  if (address == ARM_CONTROL0) return &control0;
  if (address == ARM_CONTROL1) return &control1;
  assert(address == PM_PROC);
  return &proc;
}
void udelay(uint32_t usec) {
  if (usec == 30) {
    assert(count30 < sizeof(expected));
    expected_control0 = (expected_control0 & ~(ARM_C0_BRESP1 | ARM_C0_BRESP2)) | expected[count30++];
    assert(control0 == expected_control0 && (proc & (1u << 6)));
  } else {
    assert(usec == 300 && count30 == sizeof(expected));
    if (count300++ == 0) assert(control1 & ARM_C1_REQSTOP);
    else assert(!(control1 & ARM_C1_REQSTOP));
  }
}
int main(void) {
  arm_chainloader_release_bridge();
  assert(count30 == sizeof(expected) && count300 == 2);
  assert(!(control1 & ARM_C1_REQSTOP) && (control1 & ARM_C1_PERSON));
  assert(proc == (PM_PASSWORD | (1u << 6)));
  puts("ARM bridge release sequence tests passed");
  return 0;
}
