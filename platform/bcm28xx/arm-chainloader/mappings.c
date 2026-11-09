#include <stddef.h>
#include <platform/bcm28xx/arm_chainloader.h>

#define ARM_MAP_SHIFT 24u
#define BUS_MAP_SHIFT 21u
#define UNCACHED_SDRAM_BASE 0xc0000000u
#define DEFAULT_FRAMEBUFFER_PHYS 0x06000000u

static void map(volatile uint32_t *table, uint32_t bus, uint32_t arm) {
  table[arm >> ARM_MAP_SHIFT] = bus >> BUS_MAP_SHIFT;
}

int arm_chainloader_configure_mappings(volatile uint32_t *table, size_t entries) {
  if (!table || entries < ARM_CHAINLOADER_MAP_ENTRIES)
    return -1;

  /* Mirror the preserved Pi 3 handoff, including its default and overrides. */
  for (unsigned int i = 0; i < 64; ++i)
    map(table, UNCACHED_SDRAM_BASE | DEFAULT_FRAMEBUFFER_PHYS, i << ARM_MAP_SHIFT);
  for (unsigned int i = 0; i < 62; ++i)
    map(table, UNCACHED_SDRAM_BASE | (i << ARM_MAP_SHIFT), i << ARM_MAP_SHIFT);
  map(table, 0x7e000000u, 0x20000000u);
  map(table, 0x7e000000u, 0x3f000000u);
  map(table, 0x40000000u, 0x40000000u);
  map(table, UNCACHED_SDRAM_BASE | DEFAULT_FRAMEBUFFER_PHYS, DEFAULT_FRAMEBUFFER_PHYS);
  return 0;
}
