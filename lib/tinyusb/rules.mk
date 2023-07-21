LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_SRCS += $(LOCAL_DIR)/upstream/src/tusb.c $(LOCAL_DIR)/upstream/src/host/usbh.c $(LOCAL_DIR)/upstream/src/common/tusb_fifo.c $(LOCAL_DIR)/upstream/src/host/hub.c $(LOCAL_DIR)/upstream/examples/host/bare_api/src/main.c

MODULE_SRCS += $(LOCAL_DIR)/auto_host.c $(LOCAL_DIR)/basic-host.c

GLOBAL_INCLUDES += $(LOCAL_DIR)/include $(LOCAL_DIR)/upstream/src

GLOBAL_DEFINES += CFG_TUSB_MCU=OPT_MCU_BCM2835 CFG_TUSB_DEBUG=3

include make/module.mk


