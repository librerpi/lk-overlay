LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_SRCS += $(LOCAL_DIR)/lua-utils.c

MODULES += \
	external/lua \

GLOBAL_INCLUDES += $(LOCAL_DIR)/include

include make/module.mk

