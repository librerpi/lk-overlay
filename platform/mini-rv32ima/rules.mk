LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

ARCH := riscv
SUBARCH ?= 32

PL011_UART_COUNT := 4

MODULE_DEPS += dev/interrupt/riscv_plic lib/fdt dev/virtio dev/virtio/net \
  dev/uart/pl011 \
  app/shell \


MODULE_SRCS += $(LOCAL_DIR)/platform.c \


MEMBASE ?= 0x80000000
MEMSIZE ?= 0x00100000 # default to 1MB

ARCH_RISCV_EMBEDDED := 1

include make/module.mk
