LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_SRCS += $(LOCAL_DIR)/dwc2-device.c
MODULE_SRCS += $(LOCAL_DIR)/callbacks.c

MODULE_CFLAGS += -fshort-wchar

include make/module.mk

