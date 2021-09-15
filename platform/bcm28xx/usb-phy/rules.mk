LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

#MODULES +=


MODULE_SRCS += $(LOCAL_DIR)/phy.c

include make/module.mk

