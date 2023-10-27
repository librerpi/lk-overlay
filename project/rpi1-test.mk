LOCAL_DIR := $(GET_LOCAL_DIR)

TARGET := rpi1

MODULES += \
	app/shell \
	app/stringtests \
	app/tests \
	lib/cksum \
	lib/debugcommands \
	platform/bcm28xx/sdhost \
	app/inter-arch \
	lib/gfxconsole \
	lib/gfx \
	#platform/bcm28xx/hvs-dance \
	#platform/bcm28xx/pll \
	#lib/tga \

MEMSIZE = 0xa00000

MODULES += app/linux-bootloader
