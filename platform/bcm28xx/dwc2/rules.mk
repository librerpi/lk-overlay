LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_SRCS += $(LOCAL_DIR)/dwc2.c $(LOCAL_DIR)/queue.c

#MODULES += lib/cksum-helper lib/mincrypt

include make/module.mk

