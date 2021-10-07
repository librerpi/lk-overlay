LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_SRCS += $(LOCAL_DIR)/fir.c $(LOCAL_DIR)/pink.S

include make/module.mk



