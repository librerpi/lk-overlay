TARGET := rpi3-vpu

MODULES += app/shell platform/bcm28xx/i2c platform/bcm28xx/rpi-ddr2/autoram
BOOTCODE := 1
VPU_STAGE2 := 1
GLOBAL_DEFINES += BOOTCODE=1 VPU_STAGE2=1
DEBUG := 0
