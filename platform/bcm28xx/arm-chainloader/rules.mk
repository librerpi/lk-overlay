LOCAL_DIR := $(GET_LOCAL_DIR)
MODULE := $(LOCAL_DIR)

MODULE_SRCS += $(LOCAL_DIR)/payload.S $(LOCAL_DIR)/handoff.c $(LOCAL_DIR)/mappings.c
MODULE_DEFINES += ARM_CHAINLOADER_FILE=\"$(ARM_CHAINLOADER_BIN)\"

# Always refresh the embedded input, including when a symlink target changes.
.PHONY: arm-chainloader-payload-refresh
arm-chainloader-payload-refresh:
$(BUILDDIR)/$(LOCAL_DIR)/payload.S.o: $(ARM_CHAINLOADER_BIN) arm-chainloader-payload-refresh

include make/module.mk
