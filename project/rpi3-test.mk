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
    app/inter-arch \
    app/linux-bootloader \

#GLOBAL_DEFINES += MAILBOX_FB=1
GLOBAL_DEFINES += CUSTOM_DEFAULT_STACK_SIZE=8192
GLOBAL_COMPILEFLAGS += -fstack-usage


# memory map details
# 0 + ~200kb		rpi3-test
# 16mb			raw linux kernel
# 32mb			dtb passed to linux
