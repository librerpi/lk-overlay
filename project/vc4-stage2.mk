LOCAL_DIR := $(GET_LOCAL_DIR)

TARGET := rpi3-vpu

MODULES += \
	app/shell \
	platform/bcm28xx/otp \
	platform/bcm28xx/temp \
	lib/debugcommands \
	platform/bcm28xx/vec \
	lib/gfxconsole \
	platform/bcm28xx/v3d \
	platform/bcm28xx/arm \
	#app/vc4-stage2 \
	#lib/fs/ext2 \
	lib/cksum \
	app/stringtests \
	app/tests \

#ARCH_COMPILEFLAGS += -fPIE
#ARCH_LDFLAGS += --emit-relocs --discard-none
#ARCH_LDFLAGS += --emit-relocs --discard-none --export-dynamic

# memory map details
# 0+??         rpi2-test, temporary bootloader
# 32kb + ???   uncompressed linux image
# 0 + 64mb     linux is told that this is ram
# 64mb + 20mb, VPU firmware from vc4-stage2
# 128mb        test framebuffer
# 512mb + 16mb mmio window 1
# 1008mb + 16mb mmio window 2

GLOBAL_DEFINES += PL011_TX_ONLY
