#pragma once

#include <stdint.h>

int bcm_panel_attiny_init(void);
int bcm_panel_attiny_power(int enable);
int bcm_panel_attiny_bridge_power_on(void);
int bcm_panel_attiny_set_bridge_reset(int release);
int bcm_panel_attiny_set_backlight(uint8_t brightness);
