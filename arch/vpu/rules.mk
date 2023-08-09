LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

TOOLCHAIN_PREFIX := vc4-elf-
LD := vc4-elf-ld
CC := vc4-elf-gcc
OBJCOPY := vc4-elf-objcopy
OBJDUMP := vc4-elf-objdump

$(BUILDDIR)/system-onesegment.ld: $(LOCAL_DIR)/start.ld
	@echo generating $@
	@$(MKDIR)
	echo TODO: properly template the linker script
	cp $< $@

# TODO, fix the linker flags
ARCH_LDFLAGS += -L/nix/store/cwpy4q0qvdwdif1zfwnfg5gi50c6j9w8-vc4-elf-stage-final-gcc-debug-6.5.0/lib/gcc/vc4-elf/6.2.1/
ARCH_CFLAGS += -fstack-usage -funroll-loops -Os -fno-plt
#ARCH_COMPILEFLAGS += -fno-omit-frame-pointer

MODULE_SRCS += \
	$(LOCAL_DIR)/arch.c \
	$(LOCAL_DIR)/thread.c \
	$(LOCAL_DIR)/intc.c \
	$(LOCAL_DIR)/start.S \
	$(LOCAL_DIR)/thread_asm.S \
	$(LOCAL_DIR)/interrupt.S \

MODULE_DEPS += dev/timer/vc4
GLOBAL_DEFINES += VC4_TIMER_CHANNEL=0 ARCH_HAS_MMU=0 USE_BUILTIN_ATOMICS=0

WITH_LINKER_GC ?= 1

LIBGCC := $(shell vc4-elf-gcc -print-libgcc-file-name)

include make/module.mk
