LOCAL_DIR := $(GET_LOCAL_DIR)

TARGET := rpi4-vpu

MODULES += \
	app/shell \
	lib/cksum \
	platform/bcm28xx/otp \
	lib/debugcommands \
	app/stringtests \
	app/tests \
	app/signing-dump \
	platform/bcm28xx/vec \
	platform/bcm28xx/hvs-dance \
	#platform/bcm28xx/dpi \
