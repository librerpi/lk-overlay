LOCAL_DIR := $(GET_LOCAL_DIR)
MODULE := $(LOCAL_DIR)
MODULE_SRCS += $(LOCAL_DIR)/nic.c
GLOBAL_INCLUDES += $(LOCAL_DIR)/include/

MODULE_CFLAGS := -fno-strict-aliasing

MODULES += lib/linked-list-fifo

include make/module.mk
