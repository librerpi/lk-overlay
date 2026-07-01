LOCAL_DIR := $(GET_LOCAL_DIR)
MODULE := $(LOCAL_DIR)
MODULE_SRCS += $(LOCAL_DIR)/spi.c
GLOBAL_INCLUDES += $(LOCAL_DIR)/include
MODULES += lib/cksum-helper
MODULES += lib/mincrypt
include make/module.mk
