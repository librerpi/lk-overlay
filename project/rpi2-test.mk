LOCAL_DIR := $(GET_LOCAL_DIR)

TARGET := rpi2

MODULES += \
	lib/cksum \
	platform/bcm28xx/hvs \
	platform/bcm28xx/mailbox \
	app/linux-bootloader \
	platform/bcm28xx/pll \
	app/shell \
	lib/debugcommands \
	#app/stringtests \
	#app/tests \
	#platform/bcm28xx/hvs-dance \

GLOBAL_DEFINES += CUSTOM_DEFAULT_STACK_SIZE=8192
#GLOBAL_DEFINES += PL011_TX_ONLY

GLOBAL_COMPILEFLAGS += -fstack-usage

MEMSIZE = 0xa00000

#DEBUG := 0
