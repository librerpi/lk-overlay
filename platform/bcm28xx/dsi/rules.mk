LOCAL_DIR := $(GET_LOCAL_DIR)
MODULE := $(LOCAL_DIR)

MODULE_DEPS += platform/bcm28xx/dma
MODULE_SRCS += $(LOCAL_DIR)/io.c $(LOCAL_DIR)/packet.c

include make/module.mk
