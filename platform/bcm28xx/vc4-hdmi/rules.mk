LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULES += \
	platform/bcm28xx/hvs \
	platform/bcm28xx/pixelvalve \


MODULE_SRCS += \
	$(LOCAL_DIR)/hdmi.c \

include make/module.mk
