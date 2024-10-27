LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_SRCS += $(LOCAL_DIR)/sdhost_impl.cpp $(LOCAL_DIR)/sdhost.c

MODULES += lib/bio lib/partition lib/libcpp
#MODULES += platform/bcm28xx/dma

include make/module.mk
