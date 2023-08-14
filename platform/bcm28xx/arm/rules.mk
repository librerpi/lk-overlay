LOCAL_DIR := $(GET_LOCAL_DIR)
MODULE := $(LOCAL_DIR)
MODULE_SRCS += $(LOCAL_DIR)/arm.c $(LOCAL_DIR)/payload.S

MODULES += platform/bcm28xx/power lib/cksum lib/fdt app/inter-arch

$(BUILDDIR)/platform/bcm28xx/arm/payload.S.o: build-rpi1-test/lk.bin build-rpi2-test/lk.bin build-rpi3-test/lk.bin

MODULE_INCLUDES += $(ARMSTUBS)

MODULE_DEFINES += ARM_FREQ_MHZ=$(ARM_FREQ_MHZ)

include make/module.mk
