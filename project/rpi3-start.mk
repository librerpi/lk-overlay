LOCAL_DIR := $(GET_LOCAL_DIR)

TARGET := rpi3-vpu

MODULES += \
	app/shell \
	app/stringtests \
	app/tests \
	lib/cksum \
	lib/debugcommands \
	platform/bcm28xx/otp \
	#platform/bcm28xx/hvs-dance \

#MODULES += lib/gfxconsole
MODULES += lib/fasterconsole

GLOBAL_DEFINES += PRIMARY_HVS_CHANNEL=1
MODULES += platform/bcm28xx/vec
