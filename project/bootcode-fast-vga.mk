LOCAL_DIR := $(GET_LOCAL_DIR)

TARGET := rpi3-vpu

MODULES += \
	platform/bcm28xx/otp \
	app/shell \
	#app/rom-display \
	#lib/debugcommands \
	#lib/fs/ext2 \
	#app/mountroot \

MODULES += platform/bcm28xx/dpi
MODULES += platform/bcm28xx/rpi-ddr2/autoram
#MODULES += lib/gfxconsole
MODULES += app/vpu-mandelbrot
MODULES += external/lib/libm
MODULES += platform/bcm28xx/temp

GLOBAL_DEFINES += PRIMARY_HVS_CHANNEL=0
#GLOBAL_DEFINES += UART_NO_MUX


GLOBAL_DEFINES += BOOTCODE=1
#GLOBAL_DEFINES += NOVM_MAX_ARENAS=2 NOVM_DEFAULT_ARENA=0
#GLOBAL_DEFINES += WITH_NO_FP=1
GLOBAL_DEFINES += BACKGROUND=0x0
#GLOBAL_DEFINES += CUSTOM_DEFAULT_STACK_SIZE=1024
BOOTCODE := 1
DEBUG := 1

