LOCAL_DIR := $(GET_LOCAL_DIR)

TARGET := rpi1

MODULES += \
	lib/gfxconsole \
	lib/gfx \
	app/shell \
	app/stringtests \
	app/tests \
	lib/cksum \
	lib/debugcommands \
	platform/bcm28xx/sdhost \
	platform/bcm28xx/hvs-dance \
	#platform/bcm28xx/pll \
	#lib/tga \

MEMSIZE = 0xa00000
