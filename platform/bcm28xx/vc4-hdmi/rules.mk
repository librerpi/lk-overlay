LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULES += \
	platform/bcm28xx/hvs \
	platform/bcm28xx/pixelvalve \

MODULES += lib/video_timing


MODULE_SRCS += \
	$(LOCAL_DIR)/hdmi.c \

include make/module.mk
