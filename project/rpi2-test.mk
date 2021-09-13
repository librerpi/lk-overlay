LOCAL_DIR := $(GET_LOCAL_DIR)

TARGET := rpi2

MODULES += \
	app/linux-bootloader \
	app/shell \
	lib/cksum \
	lib/debugcommands \
	lib/gfxconsole \
	platform/bcm28xx/mailbox \
	platform/bcm28xx/pll \
	#app/stringtests \
	platform/bcm28xx/hvs \
	#app/tests \
	#platform/bcm28xx/hvs-dance \

GLOBAL_DEFINES += CUSTOM_DEFAULT_STACK_SIZE=8192
#GLOBAL_DEFINES += PL011_TX_ONLY

GLOBAL_COMPILEFLAGS += -fstack-usage

MEMSIZE = 0xa00000

#DEBUG := 0
