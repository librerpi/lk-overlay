LOCAL_DIR := $(GET_LOCAL_DIR)
MODULE := $(LOCAL_DIR)
MODULE_SRCS += $(LOCAL_DIR)/list-fifo.c

include make/module.mk
