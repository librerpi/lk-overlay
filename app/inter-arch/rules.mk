LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

ifneq ($(ARCH),vpu)
MODULE_SRCS += $(LOCAL_DIR)/inter-arch.c
include make/module.mk
endif

MODULES +=

MODULE_DEPS += \
    lib/fdt

GLOBAL_INCLUDES += $(LOCAL_DIR)/include
