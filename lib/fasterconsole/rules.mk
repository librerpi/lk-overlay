LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_DEPS += \
	lib/gfx \
	lib/font

MODULE_SRCS += \
	$(LOCAL_DIR)/fasterconsole.c

include make/module.mk

