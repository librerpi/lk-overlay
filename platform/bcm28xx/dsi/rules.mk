LOCAL_DIR := $(GET_LOCAL_DIR)
MODULE := $(LOCAL_DIR)

MODULE_DEPS += platform/bcm28xx/dma platform/bcm28xx/pll platform/bcm28xx/power
MODULE_SRCS += $(LOCAL_DIR)/io.c $(LOCAL_DIR)/packet.c $(LOCAL_DIR)/phy.c

include make/module.mk
