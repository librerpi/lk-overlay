LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_DEPS += \
	lib/fs \
	lib/fs/ext2 \
	lib/partition \
	platform/bcm28xx/sdhost \

MODULE_SRCS += \
	$(LOCAL_DIR)/mountroot.c

include make/module.mk

