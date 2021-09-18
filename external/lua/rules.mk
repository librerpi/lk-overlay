LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_DEPS += external/lib/libm

MODULE_SRCS += $(LOCAL_DIR)/upstream/src/linit.c \
	$(LOCAL_DIR)/upstream/src/lstate.c \
	$(LOCAL_DIR)/upstream/src/ltm.c \
	$(LOCAL_DIR)/upstream/src/lgc.c \
	$(LOCAL_DIR)/upstream/src/ltable.c \
	$(LOCAL_DIR)/upstream/src/lobject.c \
	$(LOCAL_DIR)/upstream/src/lstring.c \
	$(LOCAL_DIR)/upstream/src/lmem.c \
	$(LOCAL_DIR)/upstream/src/ldebug.c \
	$(LOCAL_DIR)/upstream/src/ldo.c \
	$(LOCAL_DIR)/upstream/src/lvm.c \
	$(LOCAL_DIR)/upstream/src/lfunc.c \
	$(LOCAL_DIR)/upstream/src/lctype.c \
	$(LOCAL_DIR)/upstream/src/lopcodes.c \
	$(LOCAL_DIR)/upstream/src/llex.c \
	$(LOCAL_DIR)/upstream/src/lapi.c \
	$(LOCAL_DIR)/upstream/src/lparser.c \
	$(LOCAL_DIR)/upstream/src/lcode.c \
	$(LOCAL_DIR)/upstream/src/lzio.c \
	$(LOCAL_DIR)/upstream/src/lundump.c \
	$(LOCAL_DIR)/setjmp.S \
	$(LOCAL_DIR)/helper.c \

GLOBAL_INCLUDES += $(LOCAL_DIR)/upstream/src

include make/module.mk

