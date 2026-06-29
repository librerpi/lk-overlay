#pragma once

typedef struct {
  int lan_run; // the RUN pin for the lan9514
  int ethclk; // a 25 MHz clock to run the lan9514
  int status; // the status LED
  int hotplug_detect; // the HDMI hotplug detect pin
} board_pins_t;

extern const board_pins_t *current_board;

void board_init(void);
