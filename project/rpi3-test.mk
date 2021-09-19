LOCAL_DIR := $(GET_LOCAL_DIR)

TARGET := rpi3

MODULES += \
	app/shell \
	app/stringtests \
	app/tests \
	lib/cksum \
	lib/debugcommands \
    lib/gfx \
    lib/gfxconsole \

GLOBAL_DEFINES += MAILBOX_FB=1
