#pragma once

#include <stdint.h>

#define ARM_CHAINLOADER_FIRMWARE_DATA_OFFSET 32u
#define ARM_CHAINLOADER_FIRMWARE_DATA_WORDS 15u
#define ARM_CHAINLOADER_FRAMEBUFFER_MAGIC 0x3142464cu
#define ARM_CHAINLOADER_FORMAT_A8R8G8B8 1u

/* data points to the firmware-data area in the copied ARM image. */
int arm_chainloader_write_firmware_data(volatile uint32_t *data, uint32_t serial,
                                       uint32_t revision, uint32_t uart_hz,
                                       uint32_t active_framebuffer);
