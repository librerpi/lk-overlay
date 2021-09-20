LOCAL_DIR := $(GET_LOCAL_DIR)
MODULE := $(LOCAL_DIR)
MODULE_SRCS += $(LOCAL_DIR)/arm.c $(LOCAL_DIR)/payload.S

MODULES += platform/bcm28xx/power lib/cksum lib/fdt

include make/module.mk
