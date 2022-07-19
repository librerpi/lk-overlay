LOCAL_DIR := $(GET_LOCAL_DIR)

TARGET := rpi3-vpu

MODULES += \
	app/vc4-stage1 \
	platform/bcm28xx/otp \
	platform/bcm28xx/rpi-ddr2 \
	platform/bcm28xx/sdhost \
	#platform/bcm28xx/rpi-ddr2/autoram \
	app/shell \
	app/fir \

GLOBAL_DEFINES += BOOTCODE=1 NOVM_MAX_ARENAS=2 NOVM_DEFAULT_ARENA=0
GLOBAL_DEFINES += WITH_NO_FP=1
BOOTCODE := 1

WERROR := 0
