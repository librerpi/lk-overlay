LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_DEPS += \
	lib/fs \
	lib/bcache \
	lib/bio

MODULE_SRCS += \
	$(LOCAL_DIR)/zfs.c

include make/module.mk
