LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_SRCS += $(LOCAL_DIR)/mandelbrot.c $(LOCAL_DIR)/core.S

include make/module.mk



