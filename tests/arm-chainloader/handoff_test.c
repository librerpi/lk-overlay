#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <platform/bcm28xx/arm_chainloader.h>
#include <platform/bcm28xx/dsi_framebuffer.h>

int main(void) {
  assert(arm_chainloader_board_supported(0x00a02082));
  assert(arm_chainloader_board_supported(0x00a020d3));
  assert(!arm_chainloader_board_supported(0x00902082)); /* 512 MiB */
  assert(!arm_chainloader_board_supported(0x00a01041)); /* Pi 2 */
  assert(!arm_chainloader_board_supported(0x00000002)); /* old-style Pi 1 */
  const uint32_t expected[] = {
    0, 0, 0x12345678, 0x00a02082, 5389000, 0x3142464c,
    0x07000000, 0x00300000, 0x07000000, 0x07177000,
    0x07177000, 800, 480, 800, 1,
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
  uint32_t payload[32], destination[34];
  for (unsigned int i = 0; i < 32; ++i) payload[i] = 0x12340000u + i;
  memset(destination, 0xa5, sizeof(destination));
  assert(arm_chainloader_prepare_image(destination + 1, sizeof(payload), payload, sizeof(payload),
                                     0x12345678, 0x00a02082, 5389000, DSI_FB_BACK_PHYS) == 0);
  assert(destination[0] == 0xa5a5a5a5 && destination[33] == 0xa5a5a5a5);
  assert(memcmp(destination + 1, payload, ARM_CHAINLOADER_FIRMWARE_DATA_OFFSET) == 0);
  assert(memcmp(destination + 9, expected, sizeof(expected)) == 0);
  assert(memcmp(destination + 24, payload + 23, 9 * sizeof(uint32_t)) == 0);
  memset(destination, 0xa5, sizeof(destination));
  assert(arm_chainloader_prepare_image(destination + 1, sizeof(payload) - 1, payload,
                                     sizeof(payload), 1, 2, 3, DSI_FB_BACK_PHYS) == -1);
  assert(arm_chainloader_prepare_image(destination + 1, sizeof(payload), payload,
                                     91, 1, 2, 3, DSI_FB_BACK_PHYS) == -1);
  assert(arm_chainloader_prepare_image((uint8_t *)(destination + 1) + 1, sizeof(payload), payload,
                                     sizeof(payload), 1, 2, 3, DSI_FB_BACK_PHYS) == -1);
  assert(arm_chainloader_prepare_image(destination + 1, sizeof(payload), payload,
                                     sizeof(payload), 1, 2, 3, 0) == -1);
  for (unsigned int i = 0; i < 34; ++i) assert(destination[i] == 0xa5a5a5a5);
  uint32_t table[ARM_CHAINLOADER_MAP_ENTRIES + 2];
  for (unsigned int i = 0; i < ARM_CHAINLOADER_MAP_ENTRIES + 2; ++i) table[i] = 0xdeadbeef;
  assert(arm_chainloader_configure_mappings(table + 1, ARM_CHAINLOADER_MAP_ENTRIES - 1) == -1);
  for (unsigned int i = 0; i < ARM_CHAINLOADER_MAP_ENTRIES + 2; ++i) assert(table[i] == 0xdeadbeef);
  assert(arm_chainloader_configure_mappings(NULL, ARM_CHAINLOADER_MAP_ENTRIES) == -1);
  assert(arm_chainloader_configure_mappings(table + 1, ARM_CHAINLOADER_MAP_ENTRIES) == 0);
  assert(table[0] == 0xdeadbeef && table[ARM_CHAINLOADER_MAP_ENTRIES + 1] == 0xdeadbeef);
  for (unsigned int i = 0; i < ARM_CHAINLOADER_MAP_ENTRIES; ++i) {
    uint32_t expected_map = i < 62 ? 0x600u + i * 8u : 0x630u;
    if (i == 0x20 || i == 0x3f) expected_map = 0x3f0u;
    if (i == 0x40) expected_map = 0x200u;
    assert(table[i + 1] == expected_map);
  }
  puts("ARM firmware-data handoff tests passed");
  return 0;
}
