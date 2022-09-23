LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_DEPS += \
	external/lz4 \
	lib/bcache \
	lib/bio \
	lib/cksum-helper \
	lib/fs \
	lib/mincrypt \

MODULE_FLOAT_SRCS += \
	$(LOCAL_DIR)/zfs.c

include make/module.mk
