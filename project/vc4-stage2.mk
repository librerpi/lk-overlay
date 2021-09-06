LOCAL_DIR := $(GET_LOCAL_DIR)

TARGET := rpi3-vpu

MODULES += \
	app/shell \
	lib/gfxconsole \
	platform/bcm28xx/otp \
	platform/bcm28xx/temp \
	platform/bcm28xx/v3d \
	platform/bcm28xx/vec \
	#app/vc4-stage2 \
	#lib/debugcommands \
	lib/cksum \
	app/stringtests \
	app/tests \

#ARCH_COMPILEFLAGS += -fPIE
#ARCH_LDFLAGS += --emit-relocs --discard-none
#ARCH_LDFLAGS += --emit-relocs --discard-none --export-dynamic
