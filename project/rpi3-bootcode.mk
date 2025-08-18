LOCAL_DIR := $(GET_LOCAL_DIR)

TARGET := rpi3-vpu

MODULES += \
	app/shell \
	platform/bcm28xx/rpi-ddr2/autoram \
	platform/bcm28xx/dma \
	#platform/bcm28xx/otp \
	#platform/bcm28xx/dpi \
	#lib/debugcommands \
	#app/shell \
	#app/tests \
	#app/stringtests \
	#app/rpi-vpu-bootload \
	#lib/cksum \

CONFIG_DWC2 := 1
CONFIG_NET := 1

GLOBAL_DEFINES += BOOTCODE=1
#MODULES += lib/gfxconsole
#MODULES += platform/bcm28xx/vec
#GLOBAL_DEFINES += PRIMARY_HVS_CHANNEL=1
#PLLA_FREQ_MHZ := 432

#MODULES += app/vpu-mandelbrot

ifeq ($(CONFIG_DWC2),1)
  MODULES += platform/bcm28xx/dwc2
  MODULES += platform/bcm28xx/usb-phy
  MODULES += lib/tinyusb
endif

ifeq ($(CONFIG_NET),1)
  MODULES += lib/lwip
  LWIP_APP_TFTP := 1
  MODULES += lib/rpi-usb-nic
endif


BOOTCODE := 1
DEBUG := 1
