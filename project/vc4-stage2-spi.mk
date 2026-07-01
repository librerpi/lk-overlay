CONFIG_ARM_LOADER := barebox
CONFIG_ARM_LOCATION := spi
MODULES += dev/spi

include project/vc4-stage2.mk
