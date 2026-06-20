LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_SRCS += $(LOCAL_DIR)/loader.c

ifeq ($(ARCH),arm64)
MODULE_SRCS += $(LOCAL_DIR)/chain.S
endif

MODULES += \
	lib/fs lib/fs/ext2 lib/fdt lib/partition \
	app/inter-arch

ifeq ($(CONFIG_NET),1)
  MODULE_DEPS += lib/net-utils
endif

ifeq ($(CONFIG_SD_BOOT),1)
MODULES += platform/bcm28xx/sdhost
endif

include make/module.mk
