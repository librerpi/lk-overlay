LOCAL_DIR := $(GET_LOCAL_DIR)
MODULE := $(LOCAL_DIR)
MODULE_SRCS += $(LOCAL_DIR)/arm.c

ifeq ($(CONFIG_ARM_LOADER),lk)
  MODULE_SRCS += $(LOCAL_DIR)/payload.S
endif

MODULES += platform/bcm28xx/power lib/cksum lib/fdt app/inter-arch

$(BUILDDIR)/platform/bcm28xx/arm/payload.S.o: build-rpi1-test/lk.bin build-rpi2-test/lk.bin build-rpi3-test/lk.bin

MODULE_INCLUDES += $(ARMSTUBS)

MODULE_DEFINES += ARM_FREQ_MHZ=$(ARM_FREQ_MHZ)

MODULE_DEFINES += ARM_LOADER=$(CONFIG_ARM_LOADER)

ifeq ($(CONFIG_ARM_LOCATION),spi)
  MODULE_DEFINES += ARM_EMBEDDED=0 ARM_SPI=1
endif

include make/module.mk
