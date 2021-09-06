LOCAL_DIR := $(GET_LOCAL_DIR)

TARGET := rpi3-vpu

MODULES += \
	platform/bcm28xx/otp \
	app/shell \
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
