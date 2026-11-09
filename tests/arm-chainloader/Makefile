TOOLCHAIN_PREFIX ?= vc4-elf-
IMAGE_BASE ?= 0x80020000
HOST_CC ?= cc
BUILD_DIR ?= ../../build-arm-chainloader-test
PLATFORM := ../../platform/bcm28xx

.PHONY: test check
test: $(BUILD_DIR)/handoff_test $(BUILD_DIR)/clock_test
	$(BUILD_DIR)/handoff_test
	$(BUILD_DIR)/clock_test

$(BUILD_DIR)/handoff_test: handoff_test.c $(PLATFORM)/arm-chainloader/handoff.c $(PLATFORM)/arm-chainloader/mappings.c \
		$(PLATFORM)/include/platform/bcm28xx/arm_chainloader.h \
		$(PLATFORM)/include/platform/bcm28xx/dsi_framebuffer.h
	mkdir -p $(BUILD_DIR)
	$(HOST_CC) -std=c11 -Wall -Wextra -Werror -I$(PLATFORM)/include \
		handoff_test.c $(PLATFORM)/arm-chainloader/handoff.c $(PLATFORM)/arm-chainloader/mappings.c -o $@

$(BUILD_DIR)/clock_test: clock_test.c $(PLATFORM)/arm-chainloader/clock.c \
		$(PLATFORM)/include/platform/bcm28xx/arm_chainloader.h \
		$(PLATFORM)/include/platform/bcm28xx/a2w.h $(PLATFORM)/include/platform/bcm28xx/cm.h \
		include/lk/reg.h include/platform/bcm28xx.h
	mkdir -p $(BUILD_DIR)
	$(HOST_CC) -std=c11 -Wall -Wextra -Werror -Iinclude -I$(PLATFORM)/include \
		clock_test.c $(PLATFORM)/arm-chainloader/clock.c -o $@

check:
	@test -n "$(ELF)" -a -n "$(IMAGE)" -a -n "$(CHAINLOADER)" || \
		{ echo 'Set ELF, IMAGE and CHAINLOADER to the built ELF, raw LK image and ARM input'; exit 1; }
	@set -eu; \
	start=$$($(TOOLCHAIN_PREFIX)nm "$(ELF)" | awk '$$3 == "arm_chainloader_start" { print "0x" $$1 }'); \
	end=$$($(TOOLCHAIN_PREFIX)nm "$(ELF)" | awk '$$3 == "arm_chainloader_end" { print "0x" $$1 }'); \
	test -n "$$start" -a -n "$$end"; \
	size=$$((end - start)); offset=$$((start - $(IMAGE_BASE))); \
	test "$$offset" -ge 0; \
	test "$$size" -eq "$$(wc -c < "$(CHAINLOADER)")"; \
	dd if="$(IMAGE)" bs=1 skip="$$offset" count="$$size" status=none | cmp - "$(CHAINLOADER)"; \
	dd if="$(CHAINLOADER)" bs=1 skip=32 count=60 status=none | od -An -v -tu1 | \
		awk '{ for (i = 1; i <= NF; i++) { if ($$i != 0) exit 1; n++ } } END { if (n != 60) exit 1 }'; \
	echo "Embedded ARM chainloader matches input ($$size bytes at offset $$offset)"
