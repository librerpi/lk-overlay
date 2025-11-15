LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_DEPS +=

# MODULES += lib/fs

MODULE_SRCS += \
	$(LOCAL_DIR)/cksum.c

include make/module.mk

