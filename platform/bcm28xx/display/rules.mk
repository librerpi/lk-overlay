LOCAL_DIR := $(GET_LOCAL_DIR)
MODULE := $(LOCAL_DIR)

MODULE_DEPS += platform/bcm28xx/bridge platform/bcm28xx/dsi platform/bcm28xx/hvs
MODULE_SRCS += $(LOCAL_DIR)/display.c
GLOBAL_INCLUDES += $(LOCAL_DIR)/include

include make/module.mk
