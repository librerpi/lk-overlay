LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

VC4_TOOLCHAIN ?= llvm

ifeq ($(VC4_TOOLCHAIN),llvm)
  ifndef LLVM_BINDIR
    $(error LLVM_BINDIR must point to a VC4/VCE-enabled LLVM bin directory)
  endif
  ifndef VC4_SYSROOT
    $(error VC4_SYSROOT must point to a VC4 newlib sysroot)
  endif
  TOOLCHAIN_PREFIX := $(LLVM_BINDIR)/llvm-
  CC := $(LLVM_BINDIR)/clang --target=videocore-unknown-unknown-elf \
    --sysroot=$(VC4_SYSROOT) -isystem $(VC4_SYSROOT)/include -D__VC4__
  LD := $(LLVM_BINDIR)/ld.lld
  OBJCOPY := $(LLVM_BINDIR)/llvm-objcopy
  OBJDUMP := $(LLVM_BINDIR)/llvm-objdump --triple=videocore-unknown-unknown-elf
  CPPFILT := $(LLVM_BINDIR)/llvm-cxxfilt
else ifeq ($(VC4_TOOLCHAIN),gcc)
  TOOLCHAIN_PREFIX := vc4-elf-
  CC := $(TOOLCHAIN_PREFIX)gcc
  LD := $(TOOLCHAIN_PREFIX)ld
  OBJCOPY := $(TOOLCHAIN_PREFIX)objcopy
  OBJDUMP := $(TOOLCHAIN_PREFIX)objdump
  CPPFILT := $(TOOLCHAIN_PREFIX)c++filt
else
  $(error VC4_TOOLCHAIN must be either llvm or gcc)
endif

$(BUILDDIR)/system-onesegment.ld: $(LOCAL_DIR)/start.ld
	@echo generating $@
	@$(MKDIR)
	echo TODO: properly template the linker script
	cp $< $@

ARCH_CFLAGS += -fstack-usage -funroll-loops -Os
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

ifeq ($(VC4_TOOLCHAIN),llvm)
  ifndef LIBGCC
    $(error LIBGCC must contain the LLVM VC4 runtime archives)
  endif
else
  LIBGCC := $(shell $(CC) -print-libgcc-file-name)
endif

include make/module.mk
