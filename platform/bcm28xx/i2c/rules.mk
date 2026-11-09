LOCAL_DIR := $(GET_LOCAL_DIR)
MODULE := $(LOCAL_DIR)

MODULE_SRCS += platform/bcm28xx/i2c.c
MODULE_DEPS += platform/bcm28xx/pll

include make/module.mk
