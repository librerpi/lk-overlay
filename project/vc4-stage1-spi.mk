//MODULES += dev/spi
MODULES += app/shell
MODULES += lib/cksum-helper
MODULES += lib/mincrypt
MODULES += lib/debugcommands

//CONFIG_SPI_BOOT := 1
PLLC_CORE0_DIV := 2
CONFIG_HDMI := 1

include project/vc4-stage1.mk
