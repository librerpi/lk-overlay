#include <assert.h>
#include <dev/display.h>
#include <kernel/vm.h>
#include <libfdt.h>
#include <lk/err.h>
#include <lk/init.h>
#include <platform/bcm28xx/inter-arch.h>
#include <stdint.h>

uint32_t w = 0;
uint32_t h = 0;
uint32_t fb_addr = 0;
vaddr_t fb_addr_virt = 0;
uint32_t stage2_arch_init, stage2_arm_start;

inter_core_header hdr __attribute__((aligned(16))) = {
  .magic = INTER_ARCH_MAGIC,
  .header_size = sizeof(inter_core_header),
  .end_of_ram = MEMSIZE
};

bool fdt_getprop_u32(void *fdt, int nodeoffset, const char *name, uint32_t *valueOut) {
  int size;
  const uint32_t *value = fdt_getprop(fdt, nodeoffset, name, &size);
  if (!value) return false;
  if (size == sizeof(*valueOut)) {
    if (valueOut) *valueOut = fdt32_to_cpu(*value);
    return true;
  } else {
    return false;
  }
}

status_t display_get_framebuffer(struct display_framebuffer *fb) {
  if (!fb_addr_virt) {
    return ERR_NOT_SUPPORTED;
  }
  fb->image.pixels = (void*)fb_addr_virt;
  bzero(fb->image.pixels, w * h * 4);
  fb->format = DISPLAY_FORMAT_ARGB_8888;
  fb->image.format = IMAGE_FORMAT_ARGB_8888;
  fb->image.rowbytes = w * 4;
  fb->image.width = w;
  fb->image.height = h;
  fb->image.stride = w;
  fb->flush = NULL;
  return NO_ERROR;
}

#define checkerr if (ret < 0) { printf("%s():%d error %d %s\n", __FUNCTION__, __LINE__, ret, fdt_strerror(ret)); return NULL; }
static bool parse_dtb_from_vpu(void) {
  printf("hdr: %p\n", &hdr);
  printf("DTB should be at 0x%x\n", hdr.dtb_base);
  void *v_fdt = NULL;
  status_t ret2 = vmm_alloc_physical(vmm_get_kernel_aspace(),
      "dtb", PAGE_SIZE, &v_fdt, 0,
      hdr.dtb_base, 0, 0);
  assert(ret2 == NO_ERROR);
  printf("mapped 0x%x to %p\n", hdr.dtb_base, v_fdt);

  int ret = fdt_check_header(v_fdt);
  checkerr;
  uint32_t size = fdt_totalsize(v_fdt);
  if (size > PAGE_SIZE) { // need to remap with a larger size
    vmm_free_region(vmm_get_kernel_aspace(), (vaddr_t)v_fdt);
    ret2 = vmm_alloc_physical(vmm_get_kernel_aspace(),
        "dtb", ROUNDUP(size, PAGE_SIZE), &v_fdt, 0,
        hdr.dtb_base, 0, 0);
    assert(ret2 == NO_ERROR);
    printf("remapped 0x%x to %p\n", hdr.dtb_base, v_fdt);
  }
  printf("DTB size is %d\n", size);

  int depth = 0;
  int offset = 0;
  for (;;) {
    offset = fdt_next_node(v_fdt, offset, &depth);
    if (offset < 0) break;
    const char *name = fdt_get_name(v_fdt, offset, NULL);
    if (!name) continue;
    printf("offset:%d depth:%d name:%s\n", offset, depth, name);
    if (strcmp(name, "framebuffer") == 0) {
      if (!fdt_getprop_u32(v_fdt, offset, "width", &w)) puts("err1");
      if (!fdt_getprop_u32(v_fdt, offset, "height", &h)) puts("err2");
      if (!fdt_getprop_u32(v_fdt, offset, "reg", &fb_addr)) puts("err3");

      ret2 = vmm_alloc_physical(vmm_get_kernel_aspace(),
          "framebuffer", ROUNDUP(w * h * 4, PAGE_SIZE), (void **)&fb_addr_virt, 0,
          fb_addr, 0, 0);
      assert(ret2 == NO_ERROR);
      printf("%d x %d @ 0x%x / 0x%lx\n", w, h, fb_addr, fb_addr_virt);
    } else if (strcmp(name, "timestamps") == 0) {
      if (!fdt_getprop_u32(v_fdt, offset, "3stage2_arch_init", &stage2_arch_init)) puts("err4");
      if (!fdt_getprop_u32(v_fdt, offset, "4stage2_arm_start", &stage2_arm_start)) puts("err5");
    }
  }
  return true;
}


static void inter_arch_init(uint level) {
  parse_dtb_from_vpu();
}

LK_INIT_HOOK(inter_arch, &inter_arch_init, LK_INIT_LEVEL_PLATFORM - 1);
