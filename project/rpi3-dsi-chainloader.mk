include project/rpi3-vpu-stage2.mk

ifeq ($(strip $(ARM_CHAINLOADER_BIN)),)
$(error Set ARM_CHAINLOADER_BIN to the absolute path of the ARM chainloader binary)
endif
ARM_CHAINLOADER_BIN := $(abspath $(ARM_CHAINLOADER_BIN))
ifeq ($(wildcard $(ARM_CHAINLOADER_BIN)),)
$(error ARM_CHAINLOADER_BIN does not exist: $(ARM_CHAINLOADER_BIN))
endif

MODULES += platform/bcm28xx/arm-chainloader
GLOBAL_DEFINES += BCM283X_ARM_CHAINLOADER=1
