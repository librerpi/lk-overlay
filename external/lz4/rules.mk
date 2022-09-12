LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_SRCS += $(LOCAL_DIR)/lib/lz4.c

GLOBAL_INCLUDES += $(LOCAL_DIR)/lib

include make/module.mk
