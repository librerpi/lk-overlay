MODULES += app/shell
MODULES += lib/cksum-helper
MODULES += lib/mincrypt
MODULES += lib/debugcommands
//MODULES += platform/bcm28xx/v3d
MODULES += platform/bcm28xx/vce
MODULES += app/yuv
//MODULES += app/vpu-mandelbrot
MODULES += external/lib/libm

CONFIG_SPI_BOOT := 1
PLLC_CORE0_DIV := 2
CONFIG_HDMI := 1

CONFIG_DWC2 := 0
CONFIG_TINYUSB := 0
CONFIG_NET := 0

include project/vc4-stage1.mk
