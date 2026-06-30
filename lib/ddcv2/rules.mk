LOCAL_DIR := $(GET_LOCAL_DIR)

GLOBAL_INCLUDES += $(LOCAL_DIR)/include/
MODULE := $(LOCAL_DIR)
MODULES += lib/edid
MODULE_SRCS += $(LOCAL_DIR)/ddc.c

include make/module.mk

