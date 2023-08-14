LOCAL_DIR := $(GET_LOCAL_DIR)

TARGET := rpi3-vpu

MODULES += \
	platform/bcm28xx/otp \
	platform/bcm28xx/temp \
	#platform/bcm28xx/v3d \
	#app/vc4-stage2 \
	lib/cksum \
	app/stringtests \
	app/tests

#MODULES += lib/debugcommands
MODULES += platform/bcm28xx/arm
#MODULES += app/shell
#MODULES += app/yuv
#MODULES += lib/gfxconsole
MODULES += platform/bcm28xx/vec
MODULES += lib/fs/ext2
#MODULES += platform/bcm28xx/dpi
#MODULES += dev/audio
#MODULES += app/vpu-mandelbrot
#MODULES += app/bad-apple
#MODULES += app/chips-challenge
MODULES += platform/bcm28xx/usb-phy
MODULES += app/mailbox-property-server

CONFIG_DWC2 := 0
CONFIG_TINYUSB := 0
CONFIG_MANUAL_USB := 0

ifeq ($(CONFIG_DWC2),1)
  MODULES += platform/bcm28xx/dwc2
endif

ifeq ($(CONFIG_TINYUSB),1)
  MODULES += lib/tinyusb
endif

ifeq ($(CONFIG_MANUAL_USB),1)
  MODULES += lib/tinyusb/manual
endif

#GLOBAL_DEFINES += ARMSTUB=1

#ARCH_COMPILEFLAGS += -fPIE
#ARCH_LDFLAGS += --emit-relocs --discard-none
#ARCH_LDFLAGS += --emit-relocs --discard-none --export-dynamic

# memory map details, when using LK as a bare arm payload
# 0+??         rpi2-test, temporary bootloader
# 16mb + 32mb  uncompressed linux image
# 48mb + 1mb   dtb passed to linux
# 0 + 64mb     linux is told that this is ram
# 64mb + 20mb, VPU firmware from vc4-stage2
# 96mb + 16mb  test framebuffer
# 512mb + 16mb mmio window 1
# 1008mb + 16mb mmio window 2

GLOBAL_DEFINES += PL011_TX_ONLY

GLOBAL_DEFINES += PRIMARY_HVS_CHANNEL=1

# init order
# 0x80000 LK_INIT_LEVEL_ARCH
# 0x8ffff inter_arch_init
# 0x90000 LK_INIT_LEVEL_PLATFORM
