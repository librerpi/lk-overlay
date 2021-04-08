#include <app.h>
#include <arch.h>
#include <arch/ops.h>
#include <lib/fs.h>
#include <lib/partition.h>
#include <libfdt.h>
#include <platform/bcm28xx/sdhost_impl.h>
#include <platform/bcm28xx/udelay.h>
#include <stdlib.h>

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

void find_and_mount(void) {
  uint32_t sp; asm volatile("mov %0, sp": "=r"(sp)); printf("SP: 0x%x\n", sp);
  int ret;
  bdev_t *sd = rpi_sdhost_init();
  partition_publish("sdhost", 0);
  ret = fs_mount("/root", "ext2", "sdhostp1");
  if (ret) {
    printf("mount failure: %d\n", ret);
    return;
  }
}

#define KERNEL_LOAD_ADDRESS 0x81000000 // 16mb from start
#define DTB_LOAD_ADDRESS    0x82000000 // 32mb from start

bool load_kernel(void **buf, size_t *size) {
  uint32_t sp; asm volatile("mov %0, sp": "=r"(sp)); printf("SP: 0x%x\n", sp);
  filehandle *kernel, *fh;
  void *buffer = (void*)KERNEL_LOAD_ADDRESS;
  int ret;
  struct file_stat stat;
  uint64_t sizeRead;

  ret = fs_open_file("/root/zImage", &kernel);
  if (ret) {
    printf("open failed: %d\n", ret);
    return false;
  }
  ret = fs_stat_file(kernel, &stat);
  if (ret) {
    printf("failed to stat: %d\n", ret);
    return false;
  }
  //buffer = malloc(stat.size);
  printf("size is %lld, buffer 0x%x\n", stat.size, buffer);
  sizeRead = fs_read_file(kernel, buffer, 0, stat.size);
  printf("read %lld bytes\n", sizeRead);
  if (sizeRead != stat.size) {
    printf("failed to read entire file: %lld %lld\n", sizeRead, stat.size);
    //free(buffer);
    return false;
  }
  puts("closing");
  fs_close_file(kernel);

  ret = fs_open_file("/root/rpi2.dtb", &fh);
  if (ret) {
    printf("open failed: %d\n", ret);
    return false;
  }
  ret = fs_stat_file(fh, &stat);
  if (ret) {
    printf("failed to stat: %d\n", ret);
    return false;
  }
  sizeRead = fs_read_file(fh, (void*)DTB_LOAD_ADDRESS, 0, stat.size);
  if (sizeRead != stat.size) {
    printf("failed to read entire file: %lld %lld\n", sizeRead, stat.size);
    return false;
  }
  fs_close_file(fh);

  return false;
}

static void patch_dtb(void) {
  int ret;
  void* v_fdt = (void*)DTB_LOAD_ADDRESS;

  ret = fdt_open_into(v_fdt, v_fdt, 16 * 1024 * 1024);
  int chosen = fdt_path_offset(v_fdt, "/chosen");
  if (chosen < 0) panic("no chosen node in fdt");
  else {
    const char *cmdline = "print-fatal-signals=1 console=ttyAMA0,115200 earlyprintk loglevel=7";
    ret = fdt_setprop(v_fdt, chosen, "bootargs", cmdline, strlen(cmdline)+1);
  }
  int memory = fdt_path_offset(v_fdt, "/memory");
  if (memory < 0) panic("no memory node in fdt");
  else {
    struct mem_entry memmap[] = {
      { .address = htonl(1024 * 128), .size = htonl(((64) * 1024 * 1024) - (1024 * 128)) },
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
    fdt_setprop(v_fdt, soc, "ranges", (void*)ranges, sizeof(ranges));
  }
}

static void execute_linux(void) {
  puts("passing control off to linux!!!");
  arch_chain_load((void*)KERNEL_LOAD_ADDRESS, 0, ~0, 0x2000000, 0);
}

static void loader_entry(const struct app_descriptor *app, void *args) {
  uint32_t sp; asm volatile("mov %0, sp": "=r"(sp)); printf("SP: 0x%x\n", sp);
  if (arch_ints_disabled()) puts("interrupts off??");
  find_and_mount();
  load_kernel(NULL, NULL);
  patch_dtb();
  execute_linux();
}

APP_START(loader)
  //.init = property_init,
  .entry = loader_entry,
APP_END
