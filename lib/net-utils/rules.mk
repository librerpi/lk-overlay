LOCAL_DIR := $(GET_LOCAL_DIR)
MODULE := $(LOCAL_DIR)
MODULE_SRCS += $(LOCAL_DIR)/utils.c
GLOBAL_INCLUDES += $(LOCAL_DIR)/include/

MODULE_CFLAGS := -fno-strict-aliasing

include make/module.mk
