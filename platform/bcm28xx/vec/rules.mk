LOCAL_DIR := $(GET_LOCAL_DIR)
MODULE := $(LOCAL_DIR)
MODULE_SRCS += $(LOCAL_DIR)/vec.c

MODULES += platform/bcm28xx/pixelvalve platform/bcm28xx/hvs \
	platform/bcm28xx/power \
	platform/bcm28xx/hvs-dance \

ifneq ($(BOOTCODE),1)
  GLOBAL_DEFINES += WITH_TGA
  MODULES += \
	lib/tga \
	# platform/bcm28xx/hvs-dance \

endif

include make/module.mk
