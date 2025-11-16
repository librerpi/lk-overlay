#pragma once

void add_boot_target(const char *device);
void netboot_init(void);
void *load_and_run_elf(elf_handle_t *stage2_elf);
void try_to_netboot(void);
void try_sd_boot(const char *device);
