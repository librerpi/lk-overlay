LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_SRCS += $(LOCAL_DIR)/server.c

MODULES += platform/bcm28xx/mailbox

MODULE_DEFINES += GIT_HASH='"'$(shell git rev-parse HEAD)'"'

include make/module.mk

