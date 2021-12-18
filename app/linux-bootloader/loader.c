#include <app.h>
#include <arch.h>
#include <arch/ops.h>
#include <dev/display.h>
#include <kernel/thread.h>
#include <kernel/vm.h>
#include <lib/fs.h>
#include <lib/partition.h>
#include <libfdt.h>
#include <lk/debug.h>
#include <lk/err.h>
#include <lk/init.h>
#include <platform/bcm28xx/clock.h>
#include <platform/bcm28xx/inter-arch.h>
#include <platform/bcm28xx/platform.h>
#include <platform/bcm28xx/sdhost_impl.h>
#include <platform/bcm28xx/udelay.h>
#include <stdio.h>
#include <stdlib.h>

#if defined(ARCH_ARM)
#include <arch/arm/mmu.h>
#elif defined(ARCH_ARM64)
#include <arch/arm64/mmu.h>
#endif

struct mem_entry {
  uint32_t address;
  uint32_t size;
};

struct ranges {
  uint32_t child;
  uint32_t parent;
  uint32_t size;
};

void find_and_mount(void);
bool load_kernel(void**, size_t *);
void asm_set_ACTLR(uint32_t);

#define DTB_LOAD_ADDRESS    (32 * MB)
#define KERNEL_LOAD_ADDRESS (16 * MB)

void *kernel_virtual;
void *dtb_virtual;

void find_and_mount(void) {
  uint32_t sp; asm volatile("mov %0, sp": "=r"(sp)); printf("SP: 0x%x\n", sp);
  int ret;
  bdev_t *sd = rpi_sdhost_init();
  partition_publish("sdhost", 0);
  ret = fs_mount("/root", "ext2", "sdhostp1");
  if (ret) {
    printf("mount failure: %d\n", ret);
    panic("unable to mount rootfs\n");
    return;
  }
}

void *read_file(void *buffer, size_t maxSize, const char *filepath) {
  int ret;
  filehandle *fh;
  struct file_stat stat;
  uint64_t sizeRead;

  ret = fs_open_file(filepath, &fh);
  if (ret) {
    printf("failed to open %s: %d\n", filepath, ret);
    return NULL;
  }

  ret = fs_stat_file(fh, &stat);
  if (ret) {
    printf("failed to stat: %d\n", ret);
    return NULL;
  }

  if (maxSize && (stat.size > maxSize)) {
    printf("file %s is too big (%lld > %d) aborting\n", filepath, stat.size, maxSize);
    fs_close_file(fh);
    return NULL;
  }

  if (!buffer) {
    buffer = malloc(stat.size + 1);
    // incase the file is getting used as a string, make it null terminated
    ((char*)buffer)[stat.size] = 0;
  }

  printf("file size is %lld, buffer %p\n", stat.size, buffer);

  sizeRead = fs_read_file(fh, buffer, 0, stat.size);
  printf("read %lld bytes\n", sizeRead);
  if (sizeRead != stat.size) {
    printf("failed to read entire file: %lld %lld\n", sizeRead, stat.size);
    if (buffer) free(buffer);
    return NULL;
  }
  puts("closing");
  fs_close_file(fh);

  return buffer;
}

static bool map_physical(uint32_t phys_addr, void **virt_addr, uint32_t size, const char *name) {
  printf("map_physical(%x, %p, %d, %s)\n", phys_addr, virt_addr, size, name);
  status_t ret = vmm_alloc_physical(vmm_get_kernel_aspace(), name, ROUNDUP(size, PAGE_SIZE), virt_addr, 0, phys_addr, 0, 0);
  puts("done");
  return ret == NO_ERROR;
}

bool load_kernel(void **buf, size_t *size) {
  uint32_t sp; asm volatile("mov %0, sp": "=r"(sp)); printf("SP: 0x%x\n", sp);
  void *buffer;

  if (!map_physical(KERNEL_LOAD_ADDRESS, &kernel_virtual, 16 * MB, "zImage")) {
    puts("unable to map kernel");
    return false;
  }

  buffer = read_file(kernel_virtual, 16 * MB, "/root/boot/zImage");
  if (!buffer) {
    puts("failed to read kernel file");
    return false;
  }

  if (!map_physical(DTB_LOAD_ADDRESS, &dtb_virtual, 1 * MB, "raw dtb")) {
    puts("unable to map dtb");
    return false;
  }

  buffer = read_file(dtb_virtual, 1 * MB, "/root/boot/rpi2.dtb");
  if (!buffer) {
    puts("failed to read DTB file");
    return false;
  }

  printf("loaded DTB file to %p\n", dtb_virtual);

  return true;
}

static bool patch_dtb(void) {
  int ret;
  void* v_fdt = dtb_virtual;

  ret = fdt_open_into(v_fdt, v_fdt, 16 * 1024);
  if (ret) {
    printf("ERROR: fdt_open_into() == %d\n", ret);
    return false;
  }
  int chosen = fdt_path_offset(v_fdt, "/chosen");
  if (chosen < 0) {
    puts("ERROR: no chosen node in fdt");
    return false;
  } else {
    //const char *cmdline = "print-fatal-signals=1 earlyprintk loglevel=7 root=/dev/mmcblk0p2 rootdelay=10 init=/nix/store/9c3jx4prcwabhps473p44vl2c4x9rxhm-nixos-system-nixos-20.09pre-git/init console=tty1 console=ttyAMA0 user_debug=31";

    char *cmdline = read_file(NULL, 0, "/root/boot/cmdline.txt");
    if (!cmdline) {
      puts("error reading cmdline.txt");
      return false;
    }
    ret = fdt_setprop_string(v_fdt, chosen, "bootargs", cmdline);
    printf("kernel cmdline is: %s\n", cmdline);
    free(cmdline);
  }
  int memory = fdt_path_offset(v_fdt, "/memory");
  if (memory < 0) panic("no memory node in fdt");
  else {
    struct mem_entry memmap[] = {
      { .address = htonl(0), .size = htonl(64 * MB) },
      { .address = htonl(112 * MB), .size = htonl(400 * MB) },
    };
    ret = fdt_setprop(v_fdt, memory, "reg", (void*) memmap, sizeof(memmap));
  }

  int soc = fdt_path_offset(v_fdt, "/soc");
  if (soc < 0) panic("no /soc node in fdt");
  else {
    struct ranges ranges[] = {
      { .child = htonl(0x7e000000), .parent = htonl(MMIO_BASE_PHYS), .size = htonl(16 * 1024 * 1024) },
      { .child = htonl(0x40000000), .parent = htonl(0x40000000), .size = htonl(0x1000) }
    };
    //fdt_setprop(v_fdt, soc, "ranges", (void*)ranges, sizeof(ranges));
  }
  int simplefb = fdt_path_offset(v_fdt, "/system/framebuffer@8000000");
  if (simplefb < 0) {
    printf("cant find /system/framebuffer0: %d", simplefb);
  } else {
    if (false) { // disables framebuffer
      fdt_setprop_string(v_fdt, simplefb, "status", "disabled");
    } else {
      fdt_setprop_u32(v_fdt, simplefb, "width", w);
      fdt_setprop_u32(v_fdt, simplefb, "height", h);
      fdt_setprop_u32(v_fdt, simplefb, "stride", w * 4);
      fdt32_t reg[2] = { cpu_to_fdt32(fb_addr), cpu_to_fdt32(w*h*4) };
      fdt_setprop(v_fdt, simplefb, "reg", &reg, sizeof(reg));
      fdt_setprop_string(v_fdt, simplefb, "format", "a8r8g8b8");
      fdt_setprop_string(v_fdt, simplefb, "status", "okay");
    }
  }

  int unicam = fdt_path_offset(v_fdt, "/soc/csi@7e801000");
  if (unicam < 0) {
    printf("cant find unicam node: %d\n", unicam);
  } else {
    fdt_setprop_string(v_fdt, unicam, "status", "okay");
  }

  int sensor = fdt_path_offset(v_fdt, "/soc/i2cmux0/i2c@1/ov5647@36");
  if (sensor < 0) {
    printf("cant find sensor node: %d\n", sensor);
  } else {
    fdt_setprop_string(v_fdt, sensor, "status", "okay");
  }

  ret = fdt_add_subnode(v_fdt, 0, "timestamps");
  if (ret < 0) {
    printf("unable to add timestamps: %d\n", ret);
  } else {
    fdt_setprop_u32(v_fdt, ret, "3stage2_arch_init", stage2_arch_init);
    fdt_setprop_u32(v_fdt, ret, "4stage2_arm_start", stage2_arm_start);
    fdt_setprop_u32(v_fdt, ret, "5arm_platform_init", platform_init_timestamp);
    fdt_setprop_u32(v_fdt, ret, "6arm_linux_soon", *REG32(ST_CLO));
  }
  return true;
}

static void execute_linux(void) {
#ifdef ARCH_ARM
  printf("core %d passing control off to linux!!!\n", arch_curr_cpu_num());
  arch_chain_load(kernel_virtual, 0, ~0, DTB_LOAD_ADDRESS, 0);
#endif
#ifdef ARCH_ARM64
  printf("core %d passing control off to linux!!!\n", arch_curr_cpu_num());
  arch_chain_load(kernel_virtual, DTB_LOAD_ADDRESS, 0, 0, 0);
#endif
}

#ifdef ARCH_ARM64
static void arm_write_cntfrq(uint32_t val) {
  //ARM64_WRITE_SYSREG(cntfrq_el1, val);
  //ARM64_WRITE_SYSREG(cntfrq_el0, val);
}
#endif

static void prepare_arm_core(void) {
  bool need_timer = true; // FIXME, only on pi2/pi3
  bool unlock_coproc = true; // FIXME, only on pi2/pi3
  if (need_timer) {
    arm_write_cntfrq(19200000);
  }
  if (unlock_coproc) {
    // NSACR = all copros to non-sec
    //arm_write_nsacr(0x63ff);
    //arm_write_nsacr(0xffff);
  }
  //arm_write_actlr(arm_read_actlr() | 1<<6); // on cortex-A7, this is the SMP bit
  //arm_write_cpacr(0xf << 20);
  //arm_write_scr(arm_read_scr() | 0x1); // drop to non-secure mode
}

static void dump_random_arm_regs(void) {
#ifdef ARCH_ARM
  printf("ACTLR:  0x%08x\n", arm_read_actlr());
  printf("CNTFRQ: %d\n", arm_read_cntfrq());
  printf("CPACR:  0x%08x\n", arm_read_cpacr());
  printf("CPSR:   0x%08x\n", read_cpsr());
  printf("NSACR:  0x%08x\n", arm_read_nsacr());
  //printf("SCR:    0x%x\n", arm_read_scr());
#endif
}

static void loader_entry(const struct app_descriptor *app, void *args) {
  uint32_t sp; asm volatile("mov %0, sp": "=r"(sp)); printf("SP: 0x%x\n", sp);
  if (arch_ints_disabled()) puts("interrupts off??");

  find_and_mount();
  if (!load_kernel(NULL, NULL)) {
    return;
  }
  if (!patch_dtb()) {
    return;
  }
  if (false) {
    puts("running linux in 60 seconds");
    udelay(60 * 1000 * 1000);
  }
#if 0
  puts("\nBEFORE:");
  dump_random_arm_regs();
  puts("\nAFTER");
  dump_random_arm_regs();
#endif
  thread_sleep(1000);
  prepare_arm_core();
  execute_linux();
  thread_sleep(1000);
  puts("linux didnt execute, thread exiting");
}

APP_START(loader)
  .entry = loader_entry,
APP_END
