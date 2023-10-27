LOCAL_DIR := $(GET_LOCAL_DIR)

GLOBAL_INCLUDES += \
	$(LOCAL_DIR)/include

GLOBAL_DEFINES += TARGET_HAS_DEBUG_LED=1 CRYSTAL=19200000

PLATFORM := bcm28xx
ARCH := vpu

#include make/module.mk

MODULES += platform/bcm28xx/otp
