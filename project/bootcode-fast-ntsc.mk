LOCAL_DIR := $(GET_LOCAL_DIR)

TARGET := rpi3-vpu

MODULES += \
	platform/bcm28xx/otp \
	platform/bcm28xx/rpi-ddr2/autoram \
	app/shell \
	lib/debugcommands \
	#lib/fs/ext2 \
	#app/mountroot \

GRID := 1
CONSOLE := 0
PRIMARY_VIDEO := vec

ifeq ($(GRID),1)
MODULES += app/grid
endif

ifeq ($(CONSOLE),1)
MODULES += lib/gfxconsole
endif

ifeq ($(PRIMARY_VIDEO),vec)
MODULES += platform/bcm28xx/vec
GLOBAL_DEFINES += PRIMARY_HVS_CHANNEL=1
endif

ifeq ($(PRIMARY_VIDEO),dpi)
MODULES += platform/bcm28xx/dpi
GLOBAL_DEFINES += PRIMARY_HVS_CHANNEL=0
endif

#MODULES += platform/bcm28xx/sdhost

GLOBAL_DEFINES += BOOTCODE=1 NOVM_MAX_ARENAS=2 NOVM_DEFAULT_ARENA=0
#GLOBAL_DEFINES += WITH_NO_FP=1
#GLOBAL_DEFINES += CUSTOM_DEFAULT_STACK_SIZE=1024
BOOTCODE := 1
DEBUG := 2
