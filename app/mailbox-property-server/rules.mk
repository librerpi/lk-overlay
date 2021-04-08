LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_SRCS += $(LOCAL_DIR)/server.c

MODULES += platform/bcm28xx/mailbox

include make/module.mk

