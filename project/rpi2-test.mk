LOCAL_DIR := $(GET_LOCAL_DIR)

TARGET := rpi2

MODULES += \
	app/shell \
	lib/cksum \
	lib/debugcommands \
	lib/gfxconsole \
	platform/bcm28xx/mailbox \
	platform/bcm28xx/pll \
	#app/stringtests \
	platform/bcm28xx/hvs \
	#app/tests \
	#platform/bcm28xx/hvs-dance

MODULES += app/linux-bootloader
MODULES += app/inter-arch

CONFIG_DWC2 := 0
CONFIG_TINYUSB := 0

ifeq ($(CONFIG_DWC2),1)
  MODULES += platform/bcm28xx/dwc2
  #MODULES += platform/bcm28xx/usb-phy
endif

ifeq ($(CONFIG_TINYUSB),1)
  MODULES += lib/tinyusb
endif

GLOBAL_DEFINES += CUSTOM_DEFAULT_STACK_SIZE=8192
#GLOBAL_DEFINES += PL011_TX_ONLY

GLOBAL_COMPILEFLAGS += -fstack-usage

MEMSIZE = 0xa00000  # 10mb

#DEBUG := 0
