TOOLCHAIN_PREFIX ?= vc4-elf-
IMAGE_BASE ?= 0x80020000

.PHONY: check
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
	echo "Embedded ARM chainloader matches input ($$size bytes at offset $$offset)"
