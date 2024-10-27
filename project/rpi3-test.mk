LOCAL_DIR := $(GET_LOCAL_DIR)

TARGET := rpi3

MODULES += \
	app/shell \
	app/stringtests \
	app/tests \
	lib/cksum \
	lib/debugcommands \
    lib/gfx \
    app/inter-arch

MODULES += app/linux-bootloader
MODULES += lib/gfxconsole

CONFIG_DWC2 := 1
CONFIG_TINYUSB := 1
CONFIG_MANUAL_USB := 0

CONFIG_NET := 1
CONFIG_SD_BOOT := 1

ifeq ($(CONFIG_DWC2),1)
  MODULES += platform/bcm28xx/dwc2
  #MODULES += platform/bcm28xx/usb-phy
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
endif

#GLOBAL_DEFINES += MAILBOX_FB=1
GLOBAL_DEFINES += CUSTOM_DEFAULT_STACK_SIZE=8192
GLOBAL_COMPILEFLAGS += -fstack-usage

CFG_TUSB_DEBUG := 1

# memory map details
# 0 + ~200kb		rpi3-test
# 16mb			raw linux kernel
# 48mb			dtb passed to linux

ARCH_LDFLAGS += --print-memory-usage

#GLOBAL_DEFINES += PL011_TX_ONLY

WITH_LINKER_GC ?= 1
