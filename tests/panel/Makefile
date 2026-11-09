ROOT := ../..
BUILD_DIR ?= $(ROOT)/build-panel-test
HOST_CC ?= cc
PANEL := $(ROOT)/platform/bcm28xx/panel

.PHONY: test
test: $(BUILD_DIR)/panel_test
	$(BUILD_DIR)/panel_test

$(BUILD_DIR)/panel_test: panel_test.c $(PANEL)/panel.c $(PANEL)/include/platform/bcm28xx/panel.h \
		include/dev/gpio.h include/platform/bcm28xx/gpio.h \
		include/platform/bcm28xx/i2c.h include/platform/bcm28xx/udelay.h
	mkdir -p $(BUILD_DIR)
	$(HOST_CC) -std=c11 -Wall -Wextra -Werror -Iinclude -I$(PANEL)/include \
		panel_test.c $(PANEL)/panel.c -o $@
