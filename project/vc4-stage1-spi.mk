MODULES += dev/spi
MODULES += app/shell
MODULES += lib/cksum-helper
MODULES += lib/mincrypt
MODULES += platform/bcm28xx/vc4-hdmi

CONFIG_SPI_BOOT := 1
PLLC_CORE0_DIV := 2

include project/vc4-stage1.mk
