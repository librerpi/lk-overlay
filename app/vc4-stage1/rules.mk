LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_DEPS += lib/elf

ifeq ($(CONFIG_NET),1)
  MODULE_DEPS += lib/net-utils
  # MODULE_DEPS += external/lz4
  # MODULE_DEPS += lib/cksum-helper lib/mincrypt
  MODULE_SRCS += $(LOCAL_DIR)/netboot.c
endif

ifeq ($(TUH_MSC),1)
  MODULE_SRCS += $(LOCAL_DIR)/usbboot.c $(LOCAL_DIR)/fsboot.c
endif

ifeq ($(CONFIG_SD_BOOT),1)
  MODULE_SRCS += $(LOCAL_DIR)/fsboot.c
endif

MODULE_SRCS += \
	$(LOCAL_DIR)/stage1.c \

include make/module.mk

