LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

WITH_SMP := 1
#SMP_MAX_CPUS ?= 1
#LK_HEAP_IMPLEMENTATION ?= dlmalloc
MODULE_DEPS += platform/bcm28xx/power lib/hexdump

ifeq ($(ARCH),vpu)
  MODULE_DEPS += platform/bcm28xx/pll
  ifeq ($(BOOTCODE),1)
    MEMBASE := 0x80000000 # in the 8 alias
    MEMSIZE := 0x20000 # 128kb
    LINKER_SCRIPT += $(LOCAL_DIR)/bootcode.ld
  else
    MEMBASE := 0xc0000000
    MEMSIZE ?= 0x01400000 # 20MB
    LINKER_SCRIPT += $(LOCAL_DIR)/start.ld
  endif
  GLOBAL_DEFINES += SMP_MAX_CPUS=1
else # it must be arm32 or arm64
  ifeq ($(HAVE_ARM_TIMER),1)
    MODULE_DEPS += dev/timer/arm_generic
    GLOBAL_DEFINES += HAVE_ARM_TIMER=1
  else
    MODULE_DEPS += dev/timer/vc4
    GLOBAL_DEFINES += VC4_TIMER_CHANNEL=1
  endif
  MEMBASE := 0x00000000
  MODULE_SRCS += \
    $(LOCAL_DIR)/intc.c \

  LINKER_SCRIPT += $(BUILDDIR)/system-onesegment.ld
  GLOBAL_DEFINES += \
    ARM_ARCH_WAIT_FOR_SECONDARIES=0
endif


#	lib/minip \
	dev/interrupt/arm_gic \
	dev/timer/arm_cortex_a9

MODULE_SRCS += \
	$(LOCAL_DIR)/gpio.c \
	$(LOCAL_DIR)/platform.c \
	$(LOCAL_DIR)/udelay.c \
	$(LOCAL_DIR)/print_timestamp.c \
	#$(LOCAL_DIR)/i2c.c \


ifeq ($(TARGET),rpi1)
  KERNEL_BASE = 0x00000000 # FIXME, should be 0x80000000, but no-mmu requires 0
  MMIO_BASE_VIRT = 0x20000000U
  MMIO_BASE_PHYS = 0x20000000U
  KERNEL_LOAD_OFFSET := 0x00000000
  #MEMSIZE ?= 0x10000000 # 256MB
  MEMSIZE ?= 0x1400000 # 20mb
  WITH_SMP = 0
  GLOBAL_DEFINES += TARGET_HAS_DEBUG_LED=1
  MODULE_SRCS += $(LOCAL_DIR)/uart.c
else ifeq ($(TARGET),rpi2)
  # put our kernel at 0x80000000
  KERNEL_BASE = 0x80000000
  MMIO_BASE_VIRT = 0xe0000000U
  MMIO_BASE_PHYS = 0x3f000000U
  KERNEL_LOAD_OFFSET := 0x00000000
  MEMSIZE ?= 0x10000000 # 256MB
  SMP_CPU_ID_BITS := 8
  GLOBAL_DEFINES += ARM_WITH_HYP=1

  MODULE_SRCS += $(LOCAL_DIR)/uart.c
else ifeq ($(TARGET),rpi3)
  KERNEL_LOAD_OFFSET := 0x00000000
  #MEMSIZE ?= 0x40000000 # 1GB
  MEMSIZE ?= 0x1400000 # 20mb
  MMIO_BASE_VIRT = 0xffffffffc0000000ULL
  MMIO_BASE_PHYS = 0x20000000U

  GLOBAL_DEFINES += \
      MMU_WITH_TRAMPOLINE=1

  #MODULE_SRCS += $(LOCAL_DIR)/miniuart.c

  MODULE_SRCS += $(LOCAL_DIR)/uart.c

  MODULE_DEPS += \
    app/shell \
    app/tests \
    lib/fdt
else ifeq ($(TARGET),rpi3-vpu)
  MMIO_BASE_PHYS = 0x7e000000
  MMIO_BASE_VIRT = 0x7e000000
  GLOBAL_DEFINES += \

  MODULE_SRCS += \
    $(LOCAL_DIR)/uart.c \

  #MODULES += platform/bcm28xx/sdhost

else ifeq ($(TARGET),rpi4-vpu)
  MMIO_BASE_PHYS = 0x7e000000
  MMIO_BASE_VIRT = 0x7e000000
  GLOBAL_DEFINES += RPI4=1

  MODULE_SRCS += \
    $(LOCAL_DIR)/uart.c \
    $(LOCAL_DIR)/genet.c \

endif

GLOBAL_DEFINES += \
    MEMBASE=$(MEMBASE) \
    MEMSIZE=$(MEMSIZE) \
    MMIO_BASE_VIRT=$(MMIO_BASE_VIRT) \
    MMIO_BASE_PHYS=$(MMIO_BASE_PHYS) \

include make/module.mk
