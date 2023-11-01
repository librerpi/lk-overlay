LOCAL_DIR := $(GET_LOCAL_DIR)
TARGET := rpi3-vpu

MODULES += app/bad-apple
MODULES += app/shell
MODULES += lib/fs/ext2
MODULES += lib/gfxconsole
MODULES += lib/tinyusb
MODULES += platform/bcm28xx/dwc2
MODULES += platform/bcm28xx/rpi-ddr2/autoram
MODULES += platform/bcm28xx/usb-phy
MODULES += platform/bcm28xx/vec

GLOBAL_DEFINES += BOOTCODE=1 NOVM_MAX_ARENAS=2 NOVM_DEFAULT_ARENA=0
GLOBAL_DEFINES += PRIMARY_HVS_CHANNEL=1
BOOTCODE := 1
WERROR := 0

PLLC_FREQ_MHZ := 500
PLLA_FREQ_MHZ := 432
