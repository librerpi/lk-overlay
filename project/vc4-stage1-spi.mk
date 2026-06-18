MODULES += dev/spi
MODULES += app/shell
MODULES += lib/cksum-helper
MODULES += lib/mincrypt
CONFIG_SPI_BOOT := 1

include project/vc4-stage1.mk
