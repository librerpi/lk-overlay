#include <stddef.h>
#include <platform/bcm28xx/arm_chainloader.h>
#include <platform/bcm28xx/dsi_framebuffer.h>

int arm_chainloader_write_firmware_data(volatile uint32_t *data, uint32_t serial,
                                       uint32_t revision, uint32_t uart_hz,
                                       uint32_t active_framebuffer) {
  if (!data || (active_framebuffer != DSI_FB_PRIMARY_PHYS &&
                active_framebuffer != DSI_FB_BACK_PHYS))
    return -1;

  /* Matches firmware_arm_data_t and reserved indices in rpi-open-firmware. */
  data[0] = 0; /* kRamSize1GB */
  data[1] = 0; /* VPU CPUID is diagnostic only */
  data[2] = serial;
  data[3] = revision;
  data[4] = uart_hz;
  data[5] = ARM_CHAINLOADER_FRAMEBUFFER_MAGIC;
  data[6] = DSI_FB_POOL_BASE;
  data[7] = DSI_FB_POOL_SIZE;
  data[8] = DSI_FB_PRIMARY_PHYS;
  data[9] = DSI_FB_BACK_PHYS;
  data[10] = active_framebuffer;
  data[11] = DSI_FB_WIDTH;
  data[12] = DSI_FB_HEIGHT;
  data[13] = DSI_FB_WIDTH; /* stride in pixels, not bytes */
  data[14] = ARM_CHAINLOADER_FORMAT_A8R8G8B8;
  return 0;
}
