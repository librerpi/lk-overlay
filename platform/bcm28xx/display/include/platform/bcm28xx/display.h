#pragma once

#include <stdint.h>

int bcm_dsi7_scanout(void);
int bcm_dsi7_framebuffer_handoff(uint32_t *physical);
