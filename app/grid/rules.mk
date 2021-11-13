LOCAL_DIR := $(GET_LOCAL_DIR)
MODULE := $(LOCAL_DIR)
MODULE_SRCS += $(LOCAL_DIR)/grid.c
MODULES += platform/bcm28xx/hvs
include make/module.mk
