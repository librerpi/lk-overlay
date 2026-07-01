MODULES += dev/spi
# MODULES += platform/bcm28xx/v3d
# MODULES += app/shell
# MODULES += lib/debugcommands
# MODULES += app/yuv
# MODULES += app/vpu-mandelbrot

CONFIG_SPI_BOOT := 1
PLLC_CORE0_DIV := 2
CONFIG_HDMI := 1

CONFIG_DWC2 := 0
CONFIG_TINYUSB := 0
CONFIG_NET := 0

include project/vc4-stage1.mk
