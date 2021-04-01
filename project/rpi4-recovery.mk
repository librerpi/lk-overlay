LOCAL_DIR := $(GET_LOCAL_DIR)

TARGET := rpi4-vpu
BOOTCODE := 1

MODULES += \
	app/shell \
	lib/debugcommands \
	platform/bcm28xx/sdhost \
	#platform/bcm28xx/otp \
	#lib/cksum \

DEBUG := 0
