LOCAL_DIR := $(GET_LOCAL_DIR)

TARGET := rpi3-vpu

MODULES += \
	platform/bcm28xx/otp \
	platform/bcm28xx/sdhost \
	platform/bcm28xx/temp \
	platform/bcm28xx/rpi-ddr2/autoram \

#MODULES += app/shell
MODULES += platform/bcm28xx/usb-phy
MODULES += app/vc4-stage1
MODULES += lib/fs/ext2

CONFIG_DWC2 := 1
CONFIG_TINYUSB := 1
CONFIG_MANUAL_USB := 0
CONFIG_GFX := 1

ifeq ($(CONFIG_GFX),1)
  MODULES += lib/gfxconsole
  MODULES += platform/bcm28xx/vec
endif

ifeq ($(CONFIG_DWC2),1)
  MODULES += platform/bcm28xx/dwc2
  MODULES += platform/bcm28xx/usb-phy
endif

ifeq ($(CONFIG_TINYUSB),1)
  MODULES += lib/tinyusb
endif

ifeq ($(CONFIG_MANUAL_USB),1)
  MODULES += lib/tinyusb/manual
endif

GLOBAL_DEFINES += BOOTCODE=1 NOVM_MAX_ARENAS=2 NOVM_DEFAULT_ARENA=0
GLOBAL_DEFINES += PRIMARY_HVS_CHANNEL=1
#GLOBAL_DEFINES += WITH_NO_FP=1
BOOTCODE := 1

WERROR := 0

# init order
# 0x40000 LK_INIT_LEVEL_HEAP
# 0x90000 LK_INIT_LEVEL_PLATFORM
# 0x90001 sdhost_init
# 0x90005 usbphy_init
# 0x90006 dwc2_init_hook
