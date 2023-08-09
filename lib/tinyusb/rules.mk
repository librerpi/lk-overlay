LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_SRCS += $(LOCAL_DIR)/upstream/src/tusb.c $(LOCAL_DIR)/upstream/src/host/usbh.c $(LOCAL_DIR)/upstream/src/common/tusb_fifo.c $(LOCAL_DIR)/upstream/src/host/hub.c

TUH_MSC := 1

ifeq ($(TUH_MSC),1)
  MODULE_SRCS += $(LOCAL_DIR)/upstream/src/class/msc/msc_host.c
  GLOBAL_DEFINES += CFG_TUH_MSC=1
  MODULES += lib/bio lib/partition
endif

MODULE_SRCS += $(LOCAL_DIR)/auto_host.c $(LOCAL_DIR)/basic-host.c $(LOCAL_DIR)/usb_utils.c

GLOBAL_INCLUDES += $(LOCAL_DIR)/include $(LOCAL_DIR)/upstream/src

GLOBAL_DEFINES += CFG_TUSB_MCU=OPT_MCU_BCM2835 CFG_TUSB_DEBUG=0

include make/module.mk


