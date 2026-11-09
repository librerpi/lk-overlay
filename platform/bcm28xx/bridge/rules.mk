LOCAL_DIR := $(GET_LOCAL_DIR)
MODULE := $(LOCAL_DIR)

MODULE_DEPS += platform/bcm28xx/panel
MODULE_SRCS += $(LOCAL_DIR)/bridge.c
GLOBAL_INCLUDES += $(LOCAL_DIR)/include

include make/module.mk
