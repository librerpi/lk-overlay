CONFIG_ARM_LOADER := barebox
CONFIG_ARM_LOCATION := spi
CONFIG_HDMI := 1

MODULES += dev/spi

include project/vc4-stage2.mk
