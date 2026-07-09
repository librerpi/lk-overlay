CONFIG_ARM_LOADER := barebox
CONFIG_ARM_LOCATION := disk

CONFIG_DWC2 := 1
CONFIG_TINYUSB := 1
CONFIG_HDMI := 1

include project/vc4-stage2.mk
