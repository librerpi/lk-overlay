LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_SRCS += $(LOCAL_DIR)/loader.c

MODULES += platform/bcm28xx/sdhost lib/fs lib/fs/ext2 lib/fdt

include make/module.mk


