LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_SRCS += $(LOCAL_DIR)/loader.c

ifeq ($(ARCH),arm64)
MODULE_SRCS += $(LOCAL_DIR)/chain.S
endif

MODULES += \
	lib/fs lib/fs/ext2 lib/fdt lib/partition \
	platform/bcm28xx/sdhost \
	app/inter-arch

include make/module.mk
