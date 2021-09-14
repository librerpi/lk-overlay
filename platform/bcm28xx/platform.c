/*
 * Copyright (c) 2015 Travis Geiselbrecht
 *
 * Use of this source code is governed by a MIT-style
 * license that can be found in the LICENSE file or at
 * https://opensource.org/licenses/MIT
 */
#include <arch.h>
#include <dev/gpio.h>
#include <dev/uart.h>
#include <kernel/spinlock.h>
#include <lk/console_cmd.h>
#include <lk/debug.h>
#include <lk/err.h>
#include <lk/init.h>
#include <lk/reg.h>
#include <lk/trace.h>
#include <platform.h>
#include <platform/bcm28xx.h>
#include <platform/bcm28xx/a2w.h>
#include <platform/bcm28xx/clock.h>
#include <platform/bcm28xx/cm.h>
#include <platform/bcm28xx/gpio.h>
#include <platform/bcm28xx/hexdump.h>
#include <platform/bcm28xx/pll_read.h>
#include <platform/bcm28xx/power.h>
#include <platform/bcm28xx/udelay.h>
#include <platform/interrupts.h>

#if ARCH_HAS_MMU == 1
#include <kernel/vm.h>
#endif

#ifdef HAVE_ARM_TIMER
#include <dev/timer/arm_generic.h>
#endif

#if BCM2836 || BCM2835
#include <arch/arm.h>
#include <arch/arm/mmu.h>

/* initial memory mappings. parsed by start.S */
struct mmu_initial_mapping mmu_initial_mappings[] = {
    /* 1GB of sdram space */
    {
        .phys = SDRAM_BASE,
        .virt = KERNEL_BASE,
        .size = MEMSIZE,
        .flags = 0,
        .name = "memory"
    },

    /* peripherals */
    {
        .phys = BCM_PERIPH_BASE_PHYS,
        .virt = BCM_PERIPH_BASE_VIRT,
        .size = BCM_PERIPH_SIZE,
        .flags = MMU_INITIAL_MAPPING_FLAG_DEVICE,
        .name = "bcm peripherals"
    },
#ifdef WITH_SMP
    /* arm local peripherals */
    {
        .phys = 0x40000000,
        .virt = BCM_LOCAL_PERIPH_BASE_VIRT,
        .size = 1 * MB,
        .flags = MMU_INITIAL_MAPPING_FLAG_DEVICE,
        .name = "arm local peripherals"
    },
#endif
    { // 32mb window for linux+dtb
      .phys = 16 * MB,
      .virt = KERNEL_BASE + (16 * MB),
      .size = 32 * MB,
      .flags = 0,
      .name = "next-stage"
    },
    { // 1mb window for framebuffer at 128mb
      .phys = 128 * MB,
      .virt = KERNEL_BASE + (128 * MB),
      .size = 1 * MB,
      .flags = 0,
      .name = "framebuffer",
    },

    /* identity map to let the boot code run */
    {
        .phys = SDRAM_BASE,
        .virt = SDRAM_BASE,
        .size = 16*1024*1024,
        .flags = MMU_INITIAL_MAPPING_TEMPORARY
    },
    /* null entry to terminate the list */
    { 0 }
};

#define DEBUG_UART 0

#elif BCM2837
#include <libfdt.h>
#include <arch/arm64.h>
#include <arch/arm64/mmu.h>
#include <platform/mailbox.h>

/* initial memory mappings. parsed by start.S */
struct mmu_initial_mapping mmu_initial_mappings[] = {
    /* 1GB of sdram space */
    {
        .phys = SDRAM_BASE,
        .virt = KERNEL_BASE,
        .size = MEMSIZE,
        .flags = 0,
        .name = "memory"
    },

    /* peripherals */
    {
        .phys = BCM_PERIPH_BASE_PHYS,
        .virt = BCM_PERIPH_BASE_VIRT,
        .size = BCM_PERIPH_SIZE,
        .flags = MMU_INITIAL_MAPPING_FLAG_DEVICE,
        .name = "bcm peripherals"
    },
#ifdef WITH_SMP
    /* arm local peripherals */
    {
        .phys = 0x40000000,
        .virt = BCM_LOCAL_PERIPH_BASE_VIRT,
        .size = 1 * 1024 * 1024,
        .flags = MMU_INITIAL_MAPPING_FLAG_DEVICE,
        .name = "arm local peripherals"
    },
#endif

    /* null entry to terminate the list */
    { 0 }
};

#define DEBUG_UART 1

#elif ARCH_VPU
  #define DEBUG_UART 0
#else
  #error Unknown BCM28XX Variant
#endif

uint32_t vpu_clock;
// 19.2mhz for most models
// 54mhz for rpi4
uint32_t xtal_freq = CRYSTAL;

static int cmd_what_are_you(int argc, const console_cmd_args *argv) {
#ifdef ARCH_VPU
  uint32_t cpuid;
  __asm__("version %0" : "=r"(cpuid));
  printf("i am VPU with cpuid 0x%08x\n", cpuid);
#else
  printf("i am arm with MIDR 0x%x\n", arm_read_midr());
#endif
  return 0;
}

static int cmd_short_hang(int argc, const console_cmd_args *argv) {
  udelay(10 * 1000 * 1000);
  return 0;
}

static int cmd_platform_reboot(int argc, const console_cmd_args *argv) {
  platform_halt(HALT_ACTION_REBOOT, HALT_REASON_SW_RESET);
  return 0;
}

static int cmd_arm_hd(int argc, const console_cmd_args *argv) {
  uint32_t addr = 0;
  uint32_t len = 32;
  if (argc >= 2) addr = argv[1].u;
  if (argc >= 3) len = argv[2].u;

  hexdump_ram((void*)(0xc0000000 | addr), addr, len);
  return 0;
}

STATIC_COMMAND_START
STATIC_COMMAND("whatareyou", "print the cpu arch", &cmd_what_are_you)
STATIC_COMMAND("shorthang", "hang for a bit", &cmd_short_hang)
STATIC_COMMAND("r", "reboot", &cmd_platform_reboot)
STATIC_COMMAND("arm_hd", "do a hexdump, via the arm mmu mappings", &cmd_arm_hd)
STATIC_COMMAND_END(platform);

extern void intc_init(void);
extern void arm_reset(void);
static void switch_vpu_to_pllc(void);

#ifdef WITH_KERNEL_VM
static pmm_arena_t arena = {
    .name = "sdram",
    .base = SDRAM_BASE,
    .size = MEMSIZE,
    .flags = PMM_ARENA_FLAG_KMAP,
};
#endif

__WEAK uint32_t get_uart_base_freq() {
  return 48 * 1000 * 1000;
}

void platform_init_mmu_mappings(void) {
}

static void switch_vpu_to_pllc() {
  switch_vpu_to_src(CM_SRC_OSC);
  *REG32(CM_VPUDIV) = CM_PASSWORD | (1 << 12);

  int core0_div = 2;
  int per_div = 2;

  const uint64_t pllc_mhz = 108 * per_div * 4;

  setup_pllc(    pllc_mhz * 1000 * 1000, core0_div, per_div);

  int vpu_divisor = 1;
  vpu_clock = pllc_mhz / core0_div / vpu_divisor;

  const uint32_t vpu_source = CM_SRC_PLLC_CORE0;

  *REG32(CM_VPUCTL) = CM_PASSWORD | CM_VPUCTL_FRAC_SET | CM_SRC_OSC | CM_VPUCTL_GATE_SET;
  *REG32(CM_VPUDIV) = CM_PASSWORD | (vpu_divisor << 12);
  *REG32(CM_VPUCTL) = CM_PASSWORD | vpu_source | CM_VPUCTL_GATE_SET;
  *REG32(CM_VPUCTL) = CM_PASSWORD | vpu_source | CM_VPUCTL_GATE_SET | 0x10; /* ENAB */

  //*REG32(CM_TIMERDIV) = CM_PASSWORD | (19 << 12) | 819; // TODO, look into this timer
  //*REG32(CM_TIMERCTL) = CM_PASSWORD | CM_SRC_OSC | 0x10;

  int vpu = measure_clock(5);
  printf("vpu raw: %d\n", vpu);
  int pllc_core0 = vpu*vpu_divisor;
  uint32_t pllc = pllc_core0 * core0_div;
  dprintf(INFO, "VPU now at %dmhz(%d), ", vpu/1000/1000, vpu_clock);
  dprintf(INFO, "PLLC_CORE0 at %dmhz, ", pllc_core0/1000/1000);
  dprintf(INFO, "PLLC at %dmhz\n", pllc / 1000 / 1000);
  vpu_clock = vpu/1000/1000;
}

static void switch_vpu_to_crystal(void) {
  const uint32_t vpu_source = CM_SRC_OSC;
  const uint32_t vpu_divisor = 1;
  *REG32(CM_VPUCTL) = CM_PASSWORD | CM_VPUCTL_FRAC_SET | CM_SRC_OSC | CM_VPUCTL_GATE_SET;
  *REG32(CM_VPUDIV) = CM_PASSWORD | (vpu_divisor << 12);
  *REG32(CM_VPUCTL) = CM_PASSWORD | vpu_source | CM_VPUCTL_GATE_SET;
  *REG32(CM_VPUCTL) = CM_PASSWORD | vpu_source | CM_VPUCTL_GATE_SET | 0x10; /* ENAB */
}

uint8_t decode_rsts(uint32_t input) {
  return (input & BV(0)) | ((input & BV(2)) >> 1) | ((input & BV(4)) >> 2) | ((input & BV(6)) >> 3) | ((input & BV(8)) >> 4) | ((input & BV(10)) >> 5);
}

void platform_early_init(void) {
    uart_init_early();

    intc_init();

#ifdef ARCH_VPU
    uint32_t rsts = *REG32(PM_RSTS);
    uint8_t partition = decode_rsts(rsts);
    printf("PM_RSTS: 0x%x 0x%x\n", rsts, partition);
    if (rsts & PM_RSTS_HADPOR_SET) puts("  had power on reset");

    if (rsts & PM_RSTS_HADSRH_SET) puts("  had software hard reset");
    if (rsts & PM_RSTS_HADSRF_SET) puts("  had software full reset");
    if (rsts & PM_RSTS_HADSRQ_SET) puts("  had software quick reset");

    if (rsts & PM_RSTS_HADWRH_SET) puts("  had watchdog hard reset");
    if (rsts & PM_RSTS_HADWRF_SET) puts("  had watchdog full reset");
    if (rsts & PM_RSTS_HADWRQ_SET) puts("  had watchdog quick reset");

    if (rsts & PM_RSTS_HADDRH_SET) puts("  had debugger hard reset");
    if (rsts & PM_RSTS_HADDRF_SET) puts("  had debugger full reset");
    if (rsts & PM_RSTS_HADDRQ_SET) puts("  had debugger quick reset");
#ifdef BOOTCODE
    if (partition == 0x3f) {
      puts("OS requested shutdown");
      platform_halt(HALT_ACTION_HALT, HALT_REASON_SW_RESET);
    }
#endif

    if (xtal_freq == 19200000) {
      switch_vpu_to_pllc();
    } else {
      switch_vpu_to_crystal();
      int vpu = measure_clock(5);
      vpu_clock = vpu/1000/1000;
      dprintf(INFO, "VPU at %dmhz\n", vpu_clock);
      switch_vpu_to_pllc();
    }
#endif

#if BCM2835
#elif BCM2837
    arm_generic_timer_init(INTERRUPT_ARM_LOCAL_CNTPNSIRQ, 0);

    /* look for a flattened device tree just before the kernel */
    const void *fdt = (void *)KERNEL_BASE;
    int err = fdt_check_header(fdt);
    if (err >= 0) {
        /* walk the nodes, looking for 'memory' */
        int depth = 0;
        int offset = 0;
        for (;;) {
            offset = fdt_next_node(fdt, offset, &depth);
            if (offset < 0)
                break;

            /* get the name */
            const char *name = fdt_get_name(fdt, offset, NULL);
            if (!name)
                continue;

            /* look for the 'memory' property */
            if (strcmp(name, "memory") == 0) {
                printf("Found memory in fdt\n");
                int lenp;
                const void *prop_ptr = fdt_getprop(fdt, offset, "reg", &lenp);
                if (prop_ptr && lenp == 0x10) {
                    /* we're looking at a memory descriptor */
                    //uint64_t base = fdt64_to_cpu(*(uint64_t *)prop_ptr);
                    uint64_t len = fdt64_to_cpu(*((const uint64_t *)prop_ptr + 1));

                    /* trim size on certain platforms */
#if ARCH_ARM
                    if (len > 1024*1024*1024U) {
                        len = 1024*1024*1024; /* only use the first 1GB on ARM32 */
                        printf("trimming memory to 1GB\n");
                    }
#endif

                    /* set the size in the pmm arena */
                    arena.size = len;
                }
            }
        }
    }

#elif BCM2836
    arm_generic_timer_init(INTERRUPT_ARM_LOCAL_CNTPSIRQ, 1000000);
#elif ARCH_VPU
#else
#error Unknown BCM28XX Variant
#endif

#ifdef WITH_KERNEL_VM
    /* add the main memory arena */
    pmm_add_arena(&arena);
#endif

#if BCM2837
    /* reserve the first 64k of ram, which should be holding the fdt */
    struct list_node list = LIST_INITIAL_VALUE(list);
    pmm_alloc_range(MEMBASE, 0x80000 / PAGE_SIZE, &list);
#endif

#if WITH_SMP
#if BCM2837
    uintptr_t sec_entry = (uintptr_t)(&arm_reset - KERNEL_ASPACE_BASE);
    unsigned long long *spin_table = (void *)(KERNEL_ASPACE_BASE + 0xd8);

    for (uint i = 1; i <= 3; i++) {
        spin_table[i] = sec_entry;
        __asm__ __volatile__ ("" : : : "memory");
        arch_clean_cache_range(0xffff000000000000,256);
        __asm__ __volatile__("sev");
    }
#else
    /* start the other cpus */
    uintptr_t sec_entry = (uintptr_t)&arm_reset;
    sec_entry -= (KERNEL_BASE - MEMBASE);
    for (uint i = 1; i <= 3; i++) {
        *REG32(ARM_LOCAL_BASE + 0x8c + 0x10 * i) = sec_entry;
    }
#endif
#endif
    puts("done platform early init");
}

static void __attribute__(( optimize("-O1"))) benchmark_self(void) {
  volatile uint32_t temp[16];
  printf("temp is at %p, ", temp);
  //register uint32_t x __asm__("r5");
  spin_lock_saved_state_t state;
  arch_interrupt_save(&state, SPIN_LOCK_FLAG_INTERRUPTS);

  uint32_t start = *REG32(ST_CLO);
  uint32_t limit = 100000;
  asm volatile ("nop");
  for (uint32_t i=0; i<limit; i++) {
    /*asm volatile(
        //"v8ld H(0++,0), (%0+=%1) REP64"
        "v32ld HY(0++,0), (%0+=%1)"
        :
        :"r"(0x80000000), "r"(4*16));*/
    for (int j = 0; j < 16; ++j) temp[j ^0xa] = 42;
    asm volatile ("");
  }
  uint32_t stop = *REG32(ST_CLO);
  arch_interrupt_restore(state, SPIN_LOCK_FLAG_INTERRUPTS);
  uint32_t delta = stop - start;
  if (delta > 0) {
    double rate = ((float)limit) / delta;
    printf("%dMHz %f loops per tick, averaged over %d ticks, %f clocks per loop, -11: %f\n", vpu_clock, rate, delta, vpu_clock / rate, (vpu_clock/rate) - 11);
  } else {
    puts("delta zero");
  }
}

void platform_init(void) {
#if BCM2835 == 1
  gpio_config(16, kBCM2708PinmuxOut);
#endif

#ifdef RPI4
  gpio_config(42, kBCM2708PinmuxOut);
#endif
    uart_init();
    udelay(1000);
    //benchmark_self();
    printf("A2W_SMPS_A_VOLTS: 0x%x\n", *REG32(A2W_SMPS_A_VOLTS));
#if BCM2837
    init_framebuffer();
#endif
  printf("crystal is %lf MHz\n", (double)xtal_freq/1000/1000);
  printf("BCM_PERIPH_BASE_VIRT: 0x%x\n", BCM_PERIPH_BASE_VIRT);
  printf("BCM_PERIPH_BASE_PHYS: 0x%x\n", BCM_PERIPH_BASE_PHYS);
}

void platform_dputc(char c) {
    if (c == '\n')
        uart_putc(DEBUG_UART, '\r');
    uart_putc(DEBUG_UART, c);
}

int platform_dgetc(char *c, bool wait) {
    int ret = uart_getc(DEBUG_UART, wait);
    if (ret == -1)
        return -1;
    *c = ret;
    return 0;
}

void platform_halt(platform_halt_action suggested_action,
                   platform_halt_reason reason) {
  printf("PM_WDOG: 0x%x\n", PM_WDOG);
  if (suggested_action == HALT_ACTION_REBOOT) {
    dprintf(ALWAYS, "waiting for watchdog\n");
    uart_flush_tx(0);
    arch_disable_ints();
    *REG32(PM_WDOG) = PM_PASSWORD | (1 & PM_WDOG_MASK);
    uint32_t t = *REG32(PM_RSTC);
    t &= PM_RSTC_WRCFG_CLR;
    t |= 0x20;
    *REG32(PM_RSTC) = PM_PASSWORD | t;
    for (;;);
  }
  dprintf(ALWAYS, "HALT: spinning forever... (reason = %d)\n", reason);
  arch_disable_ints();
  for (;;);
}

void target_set_debug_led(unsigned int led, bool on) {
  switch (led) {
  case 0:
#ifdef RPI4
    gpio_set(42, on);
#elif BCM2835==1
    gpio_set(16, !on);
#endif
    break;
  default:
    break;
  }
}
