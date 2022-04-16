LOCAL_DIR := $(GET_LOCAL_DIR)

TARGET := rpi3-vpu

MODULES += \
	platform/bcm28xx/otp \
	platform/bcm28xx/vec \
	platform/bcm28xx/hvs-dance \
	app/shell \
	lib/debugcommands \
	app/tests \
	#app/stringtests \
	#lib/cksum \

GLOBAL_DEFINES += PL011_TX_ONLY

DEBUG := 0
