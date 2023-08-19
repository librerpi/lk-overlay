LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_SRCS += $(LOCAL_DIR)/game.c $(LOCAL_DIR)/sprites.S

MODULES += lib/tga

include make/module.mk

