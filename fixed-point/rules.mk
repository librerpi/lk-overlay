LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_SRCS += $(LOCAL_DIR)/util.cpp

MODULE_CPPFLAGS += -O3

MODULE_INCLUDES += $(LOCAL_DIR)/include/

include make/module.mk
