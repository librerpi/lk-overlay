ROOT := ../..
BUILD_DIR ?= $(ROOT)/build-dsi-test
HOST_CC ?= cc
PLATFORM := $(ROOT)/platform/bcm28xx

.PHONY: test
test: $(BUILD_DIR)/packet_test $(BUILD_DIR)/phy_test
	$(BUILD_DIR)/packet_test
	$(BUILD_DIR)/phy_test

$(BUILD_DIR)/packet_test: packet_test.c $(PLATFORM)/dsi/packet.c \
		$(PLATFORM)/include/platform/bcm28xx/dsi.h ../panel/include/platform/bcm28xx/udelay.h
	mkdir -p $(BUILD_DIR)
	$(HOST_CC) -std=c11 -Wall -Wextra -Werror -I../panel/include -I$(PLATFORM)/include \
		packet_test.c $(PLATFORM)/dsi/packet.c -o $@

$(BUILD_DIR)/phy_test: phy_test.c $(PLATFORM)/dsi/phy.c $(PLATFORM)/include/platform/bcm28xx/dsi.h \
		$(wildcard include/lk/*.h include/platform/bcm28xx/*.h) ../panel/include/platform/bcm28xx/udelay.h
	mkdir -p $(BUILD_DIR)
	$(HOST_CC) -std=c11 -Wall -Wextra -Werror -Iinclude -I../panel/include -I$(PLATFORM)/include \
		phy_test.c $(PLATFORM)/dsi/phy.c -o $@
