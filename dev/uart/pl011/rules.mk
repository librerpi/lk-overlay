LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_SRCS += $(LOCAL_DIR)/uart.c

ifeq ($(SUBARCH),arm)
  MODULE_DEFINES += HAVE_REG_IRQ
endif

ifeq ($(ARCH),riscv)
  MODULE_DEFINES += HAVE_REG_IRQ
endif

MODULE_DEFINES += NUM_UART=${PL011_UART_COUNT}

include make/module.mk
