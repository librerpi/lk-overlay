#include <app.h>
#include <arch.h>
#include <arch/ops.h>
#include <dev/display.h>
#include <kernel/event.h>
#include <kernel/mutex.h>
#include <kernel/thread.h>
#include <kernel/vm.h>
#include <lib/fs.h>
#include <lib/partition.h>
#include <libfdt.h>
#include <lk/debug.h>
#include <lk/err.h>
#include <lk/init.h>
#include <lk/trace.h>
#include <platform.h>
#include <platform/bcm28xx/clock.h>
#include <platform/bcm28xx/inter-arch.h>
#include <platform/bcm28xx/platform.h>
#include <platform/bcm28xx/print_timestamp.h>
#include <platform/bcm28xx/udelay.h>
#include <stdio.h>
#include <stdlib.h>
#include <target.h>
#ifdef WITH_LIB_TINYUSB
#include <usbhooks.h>
#endif

#if defined(ARCH_ARM)
#include <arch/arm/mmu.h>
#elif defined(ARCH_ARM64)
#include <arch/arm64/mmu.h>
#endif

//#define MB (1024*1024)
#define LOCAL_TRACE 0

struct mem_entry {
  uint32_t address;
  uint32_t size;
};

struct ranges {
  uint32_t child;
  uint32_t parent;
  uint32_t size;
};

bool load_kernel(void**, size_t *);
static bool patch_dtb(uint32_t initrd_size);
static void execute_linux(void);
static void prepare_arm_core(void);
void asm_set_ACTLR(uint32_t);
void find_and_mount(void);
static bool map_physical(uint32_t phys_addr, void **virt_addr, uint32_t size, const char *name);

#define KERNEL_LOAD_ADDRESS (16 * MB)
#define DTB_LOAD_ADDRESS    (48 * MB)
#define INITRD_LOAD_ADDRESS (49 * MB)
#define logf(fmt, ...) { print_timestamp(); printf("[LOADER:%s:%d]: "fmt, __FUNCTION__, __LINE__, ##__VA_ARGS__); }

void *kernel_virtual;
void *dtb_virtual;
void *initrd_virtual;

// TODO, put this logic into its own file and share with stage1
typedef struct {
  struct list_node node;
  const char *name;
} pending_device_t;
static mutex_t pending_device_lock = MUTEX_INITIAL_VALUE(pending_device_lock);
static struct list_node pending_devices = LIST_INITIAL_VALUE(pending_devices);
static event_t pending_devices_nonempty = EVENT_INITIAL_VALUE(pending_devices_nonempty, false, 0);

static void add_boot_target(const char *device) {
  mutex_acquire(&pending_device_lock);
  logf("considering %s as boot target\n", device);
  pending_device_t *pd = malloc(sizeof(pending_device_t));
  pd->name = device;
  list_add_tail(&pending_devices, &pd->node);
  event_signal(&pending_devices_nonempty, true);
  mutex_release(&pending_device_lock);
}

typedef struct {
  uint32_t code0;
  uint32_t code1;
  uint64_t text_offset;
  uint64_t image_size;
  uint64_t flags;
  uint64_t res2;
  uint64_t res3;
  uint64_t res4;
  uint32_t magic;
  uint32_t res5;
} linux_header_t;

static void maybe_load_initrd(size_t *size) {
  struct file_stat stat;
  filehandle *fh;
  int ret;

  *size = 0;

  ret = fs_open_file("/root/boot/initrd", &fh);
  if (ret) {
    printf("failed to open initrd: %d\n", ret);
    return;
  }

  ret = fs_stat_file(fh, &stat);
  if (ret) {
    printf("failed to stat: %d\n", ret);
    fs_close_file(fh);
    return;
  }

  if (stat.size > (16 * MB)) {
    puts("initrd too big");
    fs_close_file(fh);
    return;
  }

  if (!map_physical(INITRD_LOAD_ADDRESS, &initrd_virtual, 16 * MB, "initrd")) {
    puts("unable to map initrd");
    *size = 0;
    return;
  }

  uint64_t size_read = fs_read_file(fh, initrd_virtual, 0, stat.size);
  if (size_read != stat.size) {
    puts("partial initrd read, not using initrd");
    return;
  }
  *size = stat.size;
  fs_close_file(fh);
}

static void try_to_boot(const char *device) {
  logf("trying to boot from %s\n", device);
  int ret = fs_mount("/root", "ext2", device);
  if (ret) {
    printf("mount failure: %d\n", ret);
    return;
  }
  if (!load_kernel(NULL, NULL)) {
    goto unmount;
  }

  size_t initrd_size = 0;

  maybe_load_initrd(&initrd_size);

  if (!patch_dtb(initrd_size)) {
    // TODO, cleanup mounts
    return;
  }
  if (false) {
    puts("running linux in 60 seconds");
    udelay(60 * 1000 * 1000);
  }
  thread_sleep(1000);
#ifdef ARCH_ARM64
  linux_header_t *header = kernel_virtual;
  printf("text_offset: 0x%llx\n", header->text_offset);
  printf("image_size: 0x%llx\n", header->image_size);
  printf("flags: 0x%llx\n", header->flags);
  printf("magic: 0x%x\n", header->magic);
#endif
  //goto unmount;
  prepare_arm_core();
  execute_linux();
  thread_sleep(1000);
  puts("linux didnt execute, thread exiting");
  return;
  unmount:
    fs_unmount("/root");
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
    printf("file %s is too big (%lld > %d) aborting\n", filepath, stat.size, (uint32_t)maxSize);
    fs_close_file(fh);
    return NULL;
  }

  if (!buffer) {
    buffer = malloc(stat.size + 1);
    // incase the file is getting used as a string, make it null terminated
    ((char*)buffer)[stat.size] = 0;
  }

  logf("file size is %lld, buffer %p\n", stat.size, buffer);

  sizeRead = fs_read_file(fh, buffer, 0, stat.size);
  logf("read %lld bytes\n", sizeRead);
  if (sizeRead != stat.size) {
    printf("failed to read entire file: %lld %lld\n", sizeRead, stat.size);
    if (buffer) free(buffer);
    return NULL;
  }
  logf("closing\n");
  fs_close_file(fh);

  return buffer;
}

static bool map_physical(uint32_t phys_addr, void **virt_addr, uint32_t size, const char *name) {
  logf("map_physical(0x%x, %p, %d, %s)\n", phys_addr, virt_addr, size, name);
  status_t ret = vmm_alloc_physical(vmm_get_kernel_aspace(), name, ROUNDUP(size, PAGE_SIZE), virt_addr, 0, phys_addr, 0, 0);
  logf("done\n");
  return ret == NO_ERROR;
}

bool load_kernel(void **buf, size_t *size) {
  uint32_t sp; asm volatile("mov %0, sp": "=r"(sp)); printf("SP: 0x%x\n", sp);
  void *buffer;
  char namebuffer[64];
  const char *name;
  uint32_t type = (hw_revision >> 4) & 0xff;
  const char *kernel_suffix;

  switch (type) {
  case 0: // 1a
  case 1: // 1b
  case 2: // 1a+
  case 3: // 1b+
  case 9: // zero
  case 0xc: // zero-w
    name = "rpi1.dtb";
    kernel_suffix = "zImage-v6";
    break;
  case 4: // 2b
  case 6: // cm1
    name = "rpi2.dtb";
    kernel_suffix = "zImage-v7";
    break;
  case 8: // 3b
  case 0xa: // CM3
  case 0xd: // 3b+
  case 0xe: // 3a+
  case 0x10: // CM3+
  case 0x12: // zero-2-w
    name = "rpi3.dtb";
    kernel_suffix = "Image-aarch64.backup";
    break;
  default:
    return false;
  }

  if (!map_physical(KERNEL_LOAD_ADDRESS, &kernel_virtual, 32 * MB, "zImage")) {
    puts("unable to map kernel");
    return false;
  }

  snprintf(namebuffer, 64, "/root/boot/%s", kernel_suffix);
  buffer = read_file(kernel_virtual, 32 * MB, namebuffer);
  if (!buffer) {
    puts("failed to read kernel file");
    return false;
  }

  if (!map_physical(DTB_LOAD_ADDRESS, &dtb_virtual, 1 * MB, "raw dtb")) {
    puts("unable to map dtb");
    return false;
  }

  snprintf(namebuffer, 64, "/root/boot/%s", name);
  buffer = read_file(dtb_virtual, 1 * MB, namebuffer);
  if (!buffer) {
    puts("failed to read DTB file");
    return false;
  }

  printf("loaded DTB file to %p\n", dtb_virtual);

  return true;
}

static bool patch_dtb(uint32_t initrd_size) {
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

    if (initrd_size) {
      uint32_t value = htonl((uint32_t)INITRD_LOAD_ADDRESS);
      ret = fdt_setprop(v_fdt, chosen, "linux,initrd-start", &value, 4);
      uint32_t initrd_end = htonl((uint32_t)(INITRD_LOAD_ADDRESS + initrd_size));
      ret = fdt_setprop(v_fdt, chosen, "linux,initrd-end", &initrd_end, 4);
    }
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
    // FIXME
    //struct ranges ranges[] = {
    //  { .child = htonl(0x7e000000), .parent = htonl(MMIO_BASE_PHYS), .size = htonl(16 * 1024 * 1024) },
    //  { .child = htonl(0x40000000), .parent = htonl(0x40000000), .size = htonl(0x1000) }
    //};
    //fdt_setprop(v_fdt, soc, "ranges", (void*)ranges, sizeof(ranges));
  }
  int simplefb = fdt_path_offset(v_fdt, "/system/framebuffer@8000000");
  if (simplefb < 0) {
    printf("cant find /system/framebuffer0: %d\n", simplefb);
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

void arm_chain_load(paddr_t entry, ulong arg0, ulong arg1, ulong arg2, ulong arg3) __NO_RETURN;

#ifdef ARCH_ARM64
void arch_chain_load(void *entry, ulong arg0, ulong arg1, ulong arg2, ulong arg3) {
  // having gfxconsole active when this function prints, will cause a crash
  int ret;
  LTRACEF("entry %p, args 0x%lx 0x%lx 0x%lx 0x%lx\n", entry, arg0, arg1, arg2, arg3);

  /* we are going to shut down the system, start by disabling interrupts */
  arch_disable_ints();

  /* give target and platform a chance to put hardware into a suitable
   * state for chain loading.
   */
  target_quiesce();
  platform_quiesce();

  paddr_t entry_pa;
  paddr_t loader_pa;

#if WITH_KERNEL_VM
  /* get the physical address of the entry point we're going to branch to */
  entry_pa = vaddr_to_paddr(entry);
  if (entry_pa == 0) {
    panic("error translating entry physical address\n");
  }

  /* add the low bits of the virtual address back */
  entry_pa |= ((addr_t)entry & 0xfff);

  LTRACEF("entry pa 0x%lx\n", entry_pa);

  /* figure out the mapping for the chain load routine */
  loader_pa = vaddr_to_paddr(&arm_chain_load);
  if (loader_pa == 0) {
    panic("error translating loader physical address\n");
  }

  /* add the low bits of the virtual address back */
  loader_pa |= ((addr_t)&arm_chain_load & 0xfff);

  paddr_t loader_pa_section = ROUNDDOWN(loader_pa, SECTION_SIZE);
  LTRACEF("loader address %p, phys 0x%lx, surrounding large page 0x%lx\n", &arm_chain_load, loader_pa, loader_pa_section);

  vmm_aspace_t *myspace;
  ret = vmm_create_aspace(&myspace, "bootload", VMM_ASPACE_FLAG_NULLPAGE);
  if (ret != 0) {
    panic("Could not create new aspace %d\n", ret);
  }

  //get_current_thread()->aspace = myspace;
  vmm_set_active_aspace(myspace);
  //thread_sleep(1);

  /* using large pages, map around the target location */
  if ((ret = arch_mmu_map(&myspace->arch_aspace, loader_pa_section, loader_pa_section, (2 * SECTION_SIZE / PAGE_SIZE), 0)) != 0) {
    panic("Could not map loader into new space %d\n", ret);
  }
#else
    PANIC_UNIMPLEMENTED;
#endif
  LTRACEF("disabling instruction/data cache\n");
  udelay(1000);
  arch_disable_cache(UCACHE);

  void *foo = (void*)vaddr_to_paddr(arm_chain_load);
  LTRACEF("branching to physical address of loader, (va --> pa) (%p --> %p)\n", (void*)loader_pa, foo);

  udelay(1000);
  void (*loader)(paddr_t entry, ulong, ulong, ulong, ulong) __NO_RETURN = (void *)loader_pa;
  loader(entry_pa, arg0, arg1, arg2, arg3);
}
#endif

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
    arm_write_cntfrq(1000000);
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

  add_boot_target("sdhostp1");

  while (true) {
    event_wait(&pending_devices_nonempty);

    mutex_acquire(&pending_device_lock);

    pending_device_t *pd = list_remove_head_type(&pending_devices, pending_device_t, node);
    if (list_is_empty(&pending_devices)) event_unsignal(&pending_devices_nonempty);

    mutex_release(&pending_device_lock);

    try_to_boot(pd->name);
    free(pd);
  }

#if 0
  puts("\nBEFORE:");
  dump_random_arm_regs();
  puts("\nAFTER");
  dump_random_arm_regs();
#endif
}

static void loader_msd_probed(const char *name) {
  printf("drive %s connected\n", name);
  //char *buffer = malloc(64);
  //snprintf(buffer, 64, "%sp1", name);
  //fs_mount("/root", "ext2", buffer);
  char buffer[64];
  for (int i=0; i<4; i++) {
    snprintf(buffer, 64, "%sp%d", name, i);
    add_boot_target(strdup(buffer));
  }
}

APP_START(loader)
  .entry = loader_entry,
APP_END

#ifdef WITH_LIB_TINYUSB
USB_HOOK_START(loader)
  .msd_probed = loader_msd_probed,
USB_HOOK_END
#endif
