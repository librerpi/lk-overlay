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
#include <kernel/thread.h>
#include <lib/hexdump.h>
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
#include <platform/bcm28xx/otp.h>
#include <platform/bcm28xx/pll_read.h>
#include <platform/bcm28xx/power.h>
#include <platform/bcm28xx/print_timestamp.h>
#include <platform/bcm28xx/udelay.h>
#include <platform/interrupts.h>

#if ARCH_HAS_MMU == 1
#include <kernel/vm.h>
#endif

#ifdef HAVE_ARM_TIMER
#include <dev/timer/arm_generic.h>
#endif

#if ARCH_HAS_MMU == 1

  #if defined(ARCH_ARM)
    #include <arch/arm.h>
    #include <arch/arm/mmu.h>
  #elif defined(ARCH_ARM64)
    #include <libfdt.h>
    #include <arch/arm64.h>
    #include <arch/arm64/mmu.h>
    #include <platform/mailbox.h>
  #endif

#ifndef MB
  #define MB (1024*1024)
#endif

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
  #ifdef ARCH_ARM
    /* identity map to let the boot code run */
    {
        .phys = SDRAM_BASE,
        .virt = SDRAM_BASE,
        .size = 16*1024*1024,
        .flags = MMU_INITIAL_MAPPING_TEMPORARY
    },
  #endif
#if 0
    { // 32mb window for linux+dtb
      .phys = 16 * MB,
      .virt = KERNEL_BASE + (16 * MB),
      .size = 32 * MB,
      .flags = 0,
      .name = "next-stage"
    },

#endif
    /* null entry to terminate the list */
    { 0 }
};
#endif

#define DEBUG_UART 0

#ifdef ARCH_VPU
#define arch "VPU"
#else
#define arch "ARM"
#endif

#define logf(fmt, ...) do { print_timestamp(); printf("[" arch ":PLATFORM:%s]: " fmt, __FUNCTION__, ##__VA_ARGS__); } while(0)

static void switch_vpu_to_pllc_core0(int divisor);
extern void intc_init(void);
extern void arm_reset(void);
static void old_switch_vpu_to_pllc(void);

uint32_t vpu_clock;
// 19.2mhz for most models
// 54mhz for rpi4
uint32_t platform_init_timestamp;

static int cmd_what_are_you(int argc, const console_cmd_args *argv) {
#ifdef ARCH_VPU
  uint32_t cpuid;
  __asm__("version %0" : "=r"(cpuid));
  printf("i am VPU with cpuid 0x%08x\n", cpuid);
#elif defined(ARCH_ARM64)
  unsigned int current_el = ARM64_READ_SYSREG(CURRENTEL) >> 2;
  printf("i am aarch64 with MIDR_EL1 0x%llx in EL %d\n", ARM64_READ_SYSREG(midr_el1), current_el);
#else
  printf("i am arm with MIDR 0x%x\n", arm_read_midr());
#endif
  return 0;
}

static int cmd_short_hang(int argc, const console_cmd_args *argv) {
  spin_lock_saved_state_t state;
  arch_interrupt_save(&state, SPIN_LOCK_FLAG_INTERRUPTS);
  udelay(10 * 1000 * 1000);
  arch_interrupt_restore(state, SPIN_LOCK_FLAG_INTERRUPTS);
  return 0;
}

static int cmd_platform_reboot(int argc, const console_cmd_args *argv) {
  platform_halt(HALT_ACTION_REBOOT, HALT_REASON_SW_RESET);
  return 0;
}

#ifdef ARCH_VPU
static int cmd_arm_hd(int argc, const console_cmd_args *argv) {
  uint32_t addr = 0;
  uint32_t len = 32;
  if (argc >= 2) addr = argv[1].u;
  if (argc >= 3) len = argv[2].u;

  addr |= 0xc0000000;

  hexdump_ram((void*)addr, addr, len);
  return 0;
}
#endif

#ifdef ARCH_ARM64
static int cmd_dump_local_periph(int argc, const console_cmd_args *argv) {
  hexdump_ram((void*)BCM_LOCAL_PERIPH_BASE_VIRT, 0x40000000, 64);
  return 0;
}
#endif

static int cmd_hexdump(int argc, const console_cmd_args *argv) {
  paddr_t addr = 0;
  uint32_t len = 32;
  if (argc >= 2) addr = argv[1].u;
  if (argc >= 3) len = argv[2].u;

  hexdump_ram((void*)addr, addr, len);
  return 0;
}

static int cmd_uptime(int argc, const console_cmd_args *argv) {
  uint64_t now = current_time_hires();
  now = now / 1000000;
  uint64_t t = now;

  int seconds = t % 60;
  t = t / 60;

  int minutes = t % 60;
  t = t / 60;

  int hours = t % 24;
  t = t / 24;

  int days = t % 30;
  t = t / 30;

  if (t > 0) printf("%lld months ", t);
  if (days > 0) printf("%d days ", days);
  printf("%d:%02d:%02d\n", hours, minutes, seconds);
  return 0;
}

static int cmd_clear_rsts(int argc, const console_cmd_args *argv) {
  *REG32(PM_RSTS) = PM_PASSWORD | 0;
  return 0;
}

STATIC_COMMAND_START
STATIC_COMMAND("whatareyou", "print the cpu arch", &cmd_what_are_you)
STATIC_COMMAND("shorthang", "hang for a bit", &cmd_short_hang)
STATIC_COMMAND("r", "reboot", &cmd_platform_reboot)
STATIC_COMMAND("uptime", "show uptime", &cmd_uptime)
STATIC_COMMAND("clear_rsts", "clear PM_RSTS", &cmd_clear_rsts)
#ifdef ARCH_VPU
STATIC_COMMAND("arm_hd", "do a hexdump, via the arm mmu mappings", &cmd_arm_hd)
#endif
#ifdef ARCH_ARM64
STATIC_COMMAND("dump_local_periph", "dump arm local peripherals", cmd_dump_local_periph)
#endif
STATIC_COMMAND("hexdump", "hexdump ram", &cmd_hexdump)
STATIC_COMMAND_END(platform);

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

static void vpu_clock_updated(int core0_div, int vpu_divisor) {
  int vpu = measure_clock(5);
  //printf("vpu raw: %d\n", vpu);

  int pllc_core0 = vpu*vpu_divisor;
  uint32_t pllc = pllc_core0 * core0_div;

  dprintf(INFO, "VPU now at %dmhz(%d), ", vpu/1000/1000, vpu_clock);
  dprintf(INFO, "PLLC_CORE0 at %dmhz, ", pllc_core0/1000/1000);
  dprintf(INFO, "PLLC_PER at %lldMHz, ", freq_pllc_per / 1000 / 1000);
  dprintf(INFO, "PLLC at %dmhz\n", pllc / 1000 / 1000);
  vpu_clock = vpu/1000/1000;
}

#define A2W_XOSC_BIAS   (A2W_BASE + 0x390)
#define A2W_PLLC_CTRLR  (A2W_BASE + 0x920)
#define A2W_PLLC_CORE0R (A2W_BASE + 0xe20)
#define A2W_PLLC_MULTI  (A2W_BASE + 0xf20)

#define UNK1            0x7e500220
#define UNK2            0x7e00f100
#define UNK3            0x7e00f118

#define PM_AVS_EVENT    0x7e100084
#define A2W_PLLC_FRACR  0x7e102a20

uint32_t do_clock_math(float mult, int shift) {
  return (mult * (1 << shift)) + 8;
}

static void platform_setup_pllc_taps(int per_div, int core0_div) {
#ifdef RPI4
  *REG32(A2W_PLLC_CORE0R) = CM_PASSWORD | 6;


  *REG32(A2W_PLLC_CORE0R) = CM_PASSWORD | 0x100;

  *REG32(A2W_PLLC_MULTI) = CM_PASSWORD;

  *REG32(CM_PLLC) = CM_PASSWORD;


  *REG32(A2W_PLLC_PER) = A2W_PASSWORD | per_div;
  *REG32(A2W_PLLC_CORE0) = A2W_PASSWORD | core0_div;
  *REG32(A2W_PLLC_CORE1) = A2W_PASSWORD | 3;
  const bool core0_enable = true;
  const bool core1_enable = false;
  // which clocks to keep held when turning it all on
  const uint32_t holdflags = (!core0_enable ? CM_PLLC_HOLDCORE0_SET : 0) | (!core1_enable ? CM_PLLC_HOLDCORE1_SET : 0);
  const uint32_t loadflags = (core0_enable ? CM_PLLC_LOADCORE0_SET : 0) | (core1_enable ? CM_PLLC_LOADCORE1_SET : 0);

  *REG32(CM_PLLC) = CM_PASSWORD | CM_PLLC_DIGRST_SET |
            CM_PLLC_HOLDPER_SET | CM_PLLC_HOLDCORE2_SET |
            CM_PLLC_HOLDCORE1_SET | CM_PLLC_HOLDCORE0_SET | loadflags;

  *REG32(CM_PLLC) = CM_PASSWORD | CM_PLLC_DIGRST_SET |
            CM_PLLC_HOLDPER_SET | CM_PLLC_HOLDCORE2_SET |
            CM_PLLC_HOLDCORE1_SET | CM_PLLC_HOLDCORE0_SET;

  *REG32(CM_PLLC) = CM_PASSWORD | CM_PLLC_DIGRST_SET |
            CM_PLLC_HOLDCORE2_SET | holdflags;
#else
#endif
}

static void platform_setup_pllc(float pllc_mhz) {

  //uint64_t pllc_mhz = 108 * per_div * 4;
#ifdef RPI4
  int core0_div = 2;
  int per_div = 2;
  *REG32(A2W_XOSC_BIAS) |= CM_PASSWORD | 0x1;
  *REG32(UNK1) |= 0x200;
  *REG32(UNK2) = 0;
  *REG32(UNK3) = 3;

  uint32_t pll_mult = do_clock_math(((float)pllc_mhz) / 54, 0x14);
  printf("pll_mult: 0x%x\n", pll_mult);

  *REG32(PM_AVS_EVENT) = 0x5a800004;
  *REG32(PM_AVS_EVENT) = 0x5a000004;
  udelay(2);

  if (pllc_mhz < 800) {
    *REG32(A2W_PLLC_ANA_KAIP) = CM_PASSWORD | 0x26;
  } else if (pllc_mhz < 2000) {
    *REG32(A2W_PLLC_ANA_KAIP) = CM_PASSWORD | 0x25;
  } else {
    *REG32(A2W_PLLC_ANA_KAIP) = CM_PASSWORD | 0x23;
  }

  // set fractional and integer components of PLL multiplier
  *REG32(A2W_PLLC_FRACR) = CM_PASSWORD | (pll_mult & 0xfffff);
  pll_mult = (pll_mult >> 0x14) & 0x3ff;
  *REG32(A2W_PLLC_CTRLR) = CM_PASSWORD | 0x1000 | pll_mult;
  *REG32(A2W_PLLC_MULTI) = CM_PASSWORD;
  udelay(2);

  // wait for PLL to lock
  *REG32(CM_PLLC) = CM_PASSWORD | 0x6aa;
  int timeout = 0;
  do {
    if (0x404 & *REG32(CM_LOCK)) break;
    timeout++;
  } while (timeout < 0x2711);

  *REG32(A2W_PLLC_CTRLR) = CM_PASSWORD | pll_mult | 0x21000;
  *REG32(A2W_PLLC_MULTI) = CM_PASSWORD;

  platform_setup_pllc_taps(per_div, core0_div);
#else
#endif
}

// TODO, these claims are likely a combination of both VPU and CORE0 clocks?
// if CM_VPU is below 116mhz, the HVS cant support 1280x1024 at all
// if CM_VPU is below 116-350mhz, the HVS has trouble with v-scaling
static void old_switch_vpu_to_pllc() {
  switch_vpu_to_src(CM_SRC_OSC);
  *REG32(CM_VPUDIV) = CM_PASSWORD | (1 << 12);

  int core0_div = 1;
  int per_div = 1;

  uint64_t pllc_mhz = 100 * per_div * 5;
  //pllc_mhz = 100 * per_div * 10;

  //pllc_mhz = 108 * 9;

  printf("PLLC target %lld MHz, CORE0 %lld MHz, PER %lld MHz\n", pllc_mhz, pllc_mhz/core0_div, pllc_mhz/per_div);

  setup_pllc(    pllc_mhz * 1000 * 1000, core0_div, per_div);

  int vpu_divisor = 1;
  vpu_clock = pllc_mhz / core0_div / vpu_divisor;

  switch_vpu_to_pllc_core0(vpu_divisor);

#if 0
  // changing this divisor does impact CM_CLO/CM_CHI as expected, but complicates current_time_hires() and causes issues in code using CLO directly
  *REG32(CM_TIMERCTL) = CM_PASSWORD | 0x20;

  while (*REG32(CM_TIMERCTL) & 0x80) {}

  *REG32(CM_TIMERDIV) = CM_PASSWORD | (19 << 12) | 819;
  *REG32(CM_TIMERCTL) = CM_PASSWORD | CM_SRC_OSC | 0x10;
#endif

  vpu_clock_updated(core0_div, vpu_divisor);
}

static void switch_vpu_to_crystal(void) {
  const uint32_t vpu_source = CM_SRC_OSC;
  const uint32_t vpu_divisor = 1;
  *REG32(CM_VPUCTL) = CM_PASSWORD | CM_VPUCTL_FRAC_SET | CM_SRC_OSC | CM_VPUCTL_GATE_SET;
  *REG32(CM_VPUDIV) = CM_PASSWORD | (vpu_divisor << 12);
  *REG32(CM_VPUCTL) = CM_PASSWORD | vpu_source | CM_VPUCTL_GATE_SET;
  *REG32(CM_VPUCTL) = CM_PASSWORD | vpu_source | CM_VPUCTL_GATE_SET | 0x10; /* ENAB */
}

static void switch_vpu_to_pllc_core0(int divisor) {
  const uint32_t vpu_source = CM_SRC_PLLC_CORE0;

  *REG32(CM_VPUCTL) = CM_PASSWORD | CM_VPUCTL_FRAC_SET | CM_SRC_OSC | CM_VPUCTL_GATE_SET;
  *REG32(CM_VPUDIV) = CM_PASSWORD | (divisor << 12);
  *REG32(CM_VPUCTL) = CM_PASSWORD | vpu_source | CM_VPUCTL_GATE_SET;
  *REG32(CM_VPUCTL) = CM_PASSWORD | vpu_source | CM_VPUCTL_GATE_SET | 0x20 | 0x10; /* ENAB */
}

uint8_t decode_rsts(uint32_t input) {
  return (input & BV(0)) | ((input & BV(2)) >> 1) | ((input & BV(4)) >> 2) | ((input & BV(6)) >> 3) | ((input & BV(8)) >> 4) | ((input & BV(10)) >> 5);
}

static void pi4_pllc(void) {
  int vpu_divisor = 3;
  int core0_div = 2;

  int pllc_mhz = 108 * core0_div * 13;
  platform_setup_pllc(pllc_mhz);


  switch_vpu_to_pllc_core0(vpu_divisor);
  vpu_clock_updated(core0_div, vpu_divisor);
}

void platform_early_init(void) {
    platform_init_timestamp = *REG32(ST_CLO);
    uart_init_early();

    intc_init();

    logf("\n");

#ifdef ARCH_VPU
    uint32_t rsts = *REG32(PM_RSTS);
    uint8_t partition = decode_rsts(rsts);
    printf("PM_RSTS: 0x%x 0x%x\n", rsts, partition);
    *REG32(PM_RSTS) = PM_PASSWORD | 0;
#if 1
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
#endif

#ifdef BOOTCODE
    // if you `reboot 42` in linux, then partition will be 42
    // the NOOBS protocol uses this to load start.elf from a different fat32 partition on boot
    // a partition code of 63 is a request to shutdown
    if (partition == 63) {
      puts("OS requested shutdown");
      platform_halt(HALT_ACTION_HALT, HALT_REASON_SW_RESET);
    }
#endif

#if !defined(BOOTCODE)
    // TODO, auto-detect
    printf("MEMORY: 0x4000000 + 0x1400000: VPU firmware\n"); // copied from start.ld
#endif

    if (xtal_freq == 19200000) {
      old_switch_vpu_to_pllc();
      //setup_plla(1000 * 1000 * 1000, 10, 10);
      setup_plla(108 * 4 * 1000 * 1000, 10, 1);
    } else {
      switch_vpu_to_crystal();
      int vpu = measure_clock(5);
      vpu_clock = vpu/1000/1000;
      dprintf(INFO, "VPU at %dmhz\n", vpu_clock);
      pi4_pllc();
    }
#endif

#if BCM2835
#elif BCM2837
    // TODO, change the divisor to /1
    arm_generic_timer_init(INTERRUPT_ARM_LOCAL_CNTPNSIRQ, 1000000);

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

#if 0
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
#endif
    //logf("done platform early init\n");
}

int32_t do_fir(uint32_t rep, int16_t *coef, uint32_t stride, int16_t *input);

#ifdef ARCH_VPU
static void __attribute__(( optimize("-O1"))) benchmark_self(void) {
  volatile uint32_t temp[16];
  //register uint32_t x __asm__("r5");
  spin_lock_saved_state_t state;
  arch_interrupt_save(&state, SPIN_LOCK_FLAG_INTERRUPTS);

  int16_t *testaddr = (int16_t*)((((uint32_t)temp) & 0x3fffffff) | 0x80000000);
  printf("temp is at %p, test %p, ", temp, testaddr);

  uint32_t start = *REG32(ST_CLO);
  uint32_t limit = 100000;
  for (uint32_t i=0; i<limit; i++) {
    asm volatile ("nop");
    asm volatile ("v32min HY(0,0), HY(0,0), HY(0,0)");
    asm volatile ("v32min HY(0,0), HY(0,0), HY(0,0)");
    //asm volatile ("ld r5, (%0)" : : "r"(0xc0000000): "r5");
    /*asm volatile(
        //"v8ld H(0++,0), (%0+=%1) REP64"
        "v32ld HY(0++,0), (%0+=%1)"
        :
        :"r"(0x80000000), "r"(4*16));*/
    //for (int j = 0; j < 16; ++j) temp[j ^0xa] = 42;
    //asm volatile ("subscale r0,r0,r1<<1" : : :"r0", "r1");
    //do_fir(8, testaddr, 32, testaddr);
    //do_fir(8, testaddr, 32, testaddr);
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
#endif

void platform_init(void) {
  logf("\n");
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
#if 0
    init_framebuffer();
#endif
#if DEBUG > 0
  printf("crystal is %lf MHz\n", (double)xtal_freq/1000/1000);
  printf("BCM_PERIPH_BASE_VIRT: 0x%x\n", (int)BCM_PERIPH_BASE_VIRT);
  printf("BCM_PERIPH_BASE_PHYS: 0x%x\n", BCM_PERIPH_BASE_PHYS);
#endif
  //hdmi_init();

#ifdef ARCH_VPU
  uint32_t revision = otp_read(30);
  uint32_t type = (revision >> 4) & 0xff;
  int lan_run = 0;
  int ethclk_pin = 0;
  switch (type) {
  case 4: // 2B
    lan_run = 31;
    ethclk_pin = 44;
    break;
  case 8: // 3B
    lan_run = 29;
    ethclk_pin = 42;
    break;
  case 0xd: // 3B+
    lan_run = 30;
    ethclk_pin = 42;
    break;
  }
  if (lan_run > 0) {
    gpio_config(lan_run, kBCM2708PinmuxOut);
    gpio_set(lan_run, 0);

    if (ethclk_pin > 0) {
      // GP1 routed to GPIO42 to drive ethernet/usb chip
      *REG32(CM_GP1CTL) = CM_PASSWORD | CM_GPnCTL_KILL_SET;
      while (*REG32(CM_GP1CTL) & CM_GPnCTL_BUSY_SET) {};

      uint32_t divisor = freq_pllc_per / (25000000/0x1000);

      *REG32(CM_GP1CTL) = CM_PASSWORD | (2 << CM_GPnCTL_MASH_LSB) | CM_SRC_PLLC_CORE0;
      *REG32(CM_GP1DIV) = CM_PASSWORD | divisor; // divisor * 0x1000
      *REG32(CM_GP1CTL) = CM_PASSWORD | (2 << CM_GPnCTL_MASH_LSB) | CM_SRC_PLLC_CORE0 | CM_GPnCTL_ENAB_SET;

      gpio_config(ethclk_pin, kBCM2708Pinmux_ALT0);
    }

    udelay(10*1000);

    gpio_set(lan_run, 1);
  }
#endif
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
