LOCAL_DIR := $(GET_LOCAL_DIR)

TARGET := rpi3-vpu

MODULES += \
	platform/bcm28xx/otp \
	platform/bcm28xx/temp \
	platform/bcm28xx/rpi-ddr2/autoram \

MODULES += app/shell
MODULES += platform/bcm28xx/usb-phy
MODULES += app/vc4-stage1

#MODULES += lib/lua

CONFIG_DWC2 ?= 0
CONFIG_TINYUSB ?= 0
CONFIG_MANUAL_USB := 0
CONFIG_GFX := 0
CONFIG_NET ?= 0
CONFIG_SD_BOOT ?= 1
TUH_MSC ?= 0

ifeq ($(CONFIG_SD_BOOT),1)
  MODULES += platform/bcm28xx/sdhost
  CONFIG_DISK := 1
endif

ifeq ($(TUH_MSC),1)
  CONFIG_DISK := 1
endif

ifeq ($(CONFIG_DISK),1)
  MODULES += lib/fs/ext2 lib/partition lib/fs
endif

ifeq ($(CONFIG_GFX),1)
  MODULES += lib/gfxconsole
  MODULES += platform/bcm28xx/vec
  PLLA_FREQ_MHZ := 432
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

ifeq ($(CONFIG_NET),1)
  MODULES += lib/lwip
  MODULES += lib/rpi-usb-nic
  LWIP_APP_TFTP := 1
  LWIP_APP_HTTP := 1
endif

GLOBAL_DEFINES += BOOTCODE=1 NOVM_MAX_ARENAS=2 NOVM_DEFAULT_ARENA=0
GLOBAL_DEFINES += PRIMARY_HVS_CHANNEL=1
GLOBAL_DEFINES += WITH_NO_FP=0
BOOTCODE := 1

WERROR := 0

# init order
# 0x40000 LK_INIT_LEVEL_HEAP
# 0x90000 LK_INIT_LEVEL_PLATFORM
# 0x90001 sdhost_init
# 0x90005 usbphy_init
# 0x90006 dwc2_init_hook
# 0xa0000 LK_INIT_LEVEL_TARGET
# 0xb0000 LK_INIT_LEVEL_APPS
#   an app thread manages the tinyusb polling
#     callbacks from tinyusb start the ethernet driver
