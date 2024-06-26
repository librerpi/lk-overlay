#pragma once

#include <lib/bio.h>

#ifdef __cplusplus
extern "C" {
#endif

bdev_t *rpi_sdhost_init(void);

void rpi_sdhost_set_clock(uint32_t clock_div);

#ifdef __cplusplus
}
#endif
