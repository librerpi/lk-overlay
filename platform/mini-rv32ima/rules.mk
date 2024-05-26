LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

ARCH := riscv
SUBARCH ?= 32

MODULE_DEPS += dev/interrupt/riscv_plic lib/fdt dev/virtio dev/virtio/net

MODULE_SRCS += $(LOCAL_DIR)/platform.c

MEMBASE ?= 0x80000000
MEMSIZE ?= 0x00100000 # default to 1MB

ARCH_RISCV_EMBEDDED := 1

include make/module.mk
