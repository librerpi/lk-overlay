LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_SRCS += $(LOCAL_DIR)/bad-apple.c

MODULES += lib/tga lib/gfx dev/audio

MODULES += lib/cksum-helper lib/mincrypt

include make/module.mk

