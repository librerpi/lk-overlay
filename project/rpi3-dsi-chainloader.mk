include project/rpi3-vpu-stage2.mk

ifeq ($(strip $(ARM_CHAINLOADER_BIN)),)
$(error Set ARM_CHAINLOADER_BIN to the absolute path of the ARM chainloader binary)
endif
ARM_CHAINLOADER_BIN := $(abspath $(ARM_CHAINLOADER_BIN))
ifeq ($(wildcard $(ARM_CHAINLOADER_BIN)),)
$(error ARM_CHAINLOADER_BIN does not exist: $(ARM_CHAINLOADER_BIN))
endif

MODULES += platform/bcm28xx/arm-chainloader
# Keep the payload in the image until the ARM handoff is connected.
GLOBAL_LDFLAGS += -u arm_chainloader_start -u arm_chainloader_prepare_image \
	-u arm_chainloader_configure_mappings -u arm_chainloader_setup_clock
