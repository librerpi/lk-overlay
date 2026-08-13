LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULES += lib/hexdump
MODULE_DEPS += platform/bcm28xx/power
# for the hvs yuv image this borrows to display from - vce_display
MODULE_DEPS += platform/bcm28xx/hvs

MODULE_SRCS += \
	$(LOCAL_DIR)/vce.c \

include make/module.mk
