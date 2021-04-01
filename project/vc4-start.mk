LOCAL_DIR := $(GET_LOCAL_DIR)

TARGET := rpi3-vpu

MODULES += \
	platform/bcm28xx/otp \
	platform/bcm28xx/vec \
	platform/bcm28xx/hvs-dance \
	platform/bcm28xx/arm \
	#app/shell \
	#lib/debugcommands \
	#app/tests \
	#app/stringtests \
	#lib/cksum \

GLOBAL_DEFINES += PL011_TX_ONLY
