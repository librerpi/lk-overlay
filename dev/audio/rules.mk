LOCAL_DIR := $(GET_LOCAL_DIR)
MODULE := $(LOCAL_DIR)
MODULES += external/lib/libm
MODULE_SRCS += $(LOCAL_DIR)/audio.c
include make/module.mk
