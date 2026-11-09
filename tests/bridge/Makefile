ROOT := ../..
BUILD_DIR ?= $(ROOT)/build-bridge-test
HOST_CC ?= cc
PLATFORM := $(ROOT)/platform/bcm28xx

.PHONY: test
test: $(BUILD_DIR)/bridge_test
	$(BUILD_DIR)/bridge_test

$(BUILD_DIR)/bridge_test: bridge_test.c $(PLATFORM)/bridge/bridge.c \
		$(PLATFORM)/include/platform/bcm28xx/dsi.h \
		$(PLATFORM)/bridge/include/platform/bcm28xx/bridge.h \
		$(PLATFORM)/panel/include/platform/bcm28xx/panel.h \
		../panel/include/platform/bcm28xx/udelay.h
	mkdir -p $(BUILD_DIR)
	$(HOST_CC) -std=c11 -Wall -Wextra -Werror -I../panel/include \
		-I$(PLATFORM)/include -I$(PLATFORM)/bridge/include -I$(PLATFORM)/panel/include \
		bridge_test.c $(PLATFORM)/bridge/bridge.c -o $@
