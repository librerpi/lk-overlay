LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_DEPS += \
	lib/elf \
	lib/fs \
	lib/fs/ext2 \
	lib/partition \

MODULE_SRCS += \
	$(LOCAL_DIR)/stage1.c \

include make/module.mk

