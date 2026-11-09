#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <platform/bcm28xx/arm_chainloader.h>
#include <platform/bcm28xx/dsi_framebuffer.h>

int main(void) {
  const uint32_t expected[] = {
    0, 0, 0x12345678, 0x00a02082, 5389000, 0x3142464c,
    0x01800000, 0x00300000, 0x01800000, 0x01977000,
    0x01977000, 800, 480, 800, 1,
  };
  uint32_t data[ARM_CHAINLOADER_FIRMWARE_DATA_WORDS + 2];
  _Static_assert(sizeof(expected) == ARM_CHAINLOADER_FIRMWARE_DATA_WORDS * sizeof(uint32_t), "ABI size");
  memset(data, 0xa5, sizeof(data));
  assert(arm_chainloader_write_firmware_data(data + 1, 0x12345678, 0x00a02082,
                                             5389000, DSI_FB_BACK_PHYS) == 0);
  assert(data[0] == 0xa5a5a5a5 && data[16] == 0xa5a5a5a5);
  assert(memcmp(data + 1, expected, sizeof(expected)) == 0);
  assert(arm_chainloader_write_firmware_data(data + 1, 1, 2, 3, DSI_FB_PRIMARY_PHYS) == 0);
  assert(data[11] == DSI_FB_PRIMARY_PHYS);
  memset(data, 0xa5, sizeof(data));
  assert(arm_chainloader_write_firmware_data(data + 1, 1, 2, 3, DSI_FB_BACK_PHYS + 4) == -1);
  for (unsigned int i = 0; i < sizeof(data) / sizeof(data[0]); ++i)
    assert(data[i] == 0xa5a5a5a5);
  assert(arm_chainloader_write_firmware_data(NULL, 1, 2, 3, DSI_FB_BACK_PHYS) == -1);
  puts("ARM firmware-data handoff tests passed");
  return 0;
}
