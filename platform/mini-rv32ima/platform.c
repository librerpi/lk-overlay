#include <dev/interrupt/riscv_plic.h>
#include <dev/virtio.h>
#include <dev/virtio/net.h>
#include <lib/cbuf.h>
#include <libfdt.h>
#include <lk/reg.h>
#include <platform.h>
#include <stdbool.h>

static cbuf_t rxbuf;
#define RXBUF_SIZE 16
extern ulong lk_boot_args[4];

void platform_early_init(void) {
  plic_early_init(0x10400000, 32, false);
}

void platform_init() {
  cbuf_initialize(&rxbuf, RXBUF_SIZE);
  printf("0x%lx\n", lk_boot_args[1]);
  const void *fdt = (void*)lk_boot_args[1];
  int err = fdt_check_header(fdt);
  if (err >= 0) {
    puts("valid DTB found");
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

      printf("%d %d %s\n", depth, offset, name);
      int lenp;
      const char *prop_ptr = fdt_getprop(fdt, offset, "compatible", &lenp);
      if (prop_ptr) { // has a compatible=
        //printf("prop %p %d %s\n", prop_ptr, lenp, prop_ptr);
        if (strcmp(prop_ptr, "virtio,mmio") == 0) {
          const uint32_t *reg = fdt_getprop(fdt, offset, "reg", NULL);
          uint32_t mmio_base = fdt32_to_cpu(reg[0]);
          reg = fdt_getprop(fdt, offset, "interrupts", NULL);
          uint32_t irq = fdt32_to_cpu(reg[0]);
          printf("virtio at 0x%x %d\n", mmio_base, irq);
          //virtio_mmio_detect((void*)mmio_base, 1, &irq, 0);
        }
      } else {
      }
    }
  }
}

void platform_dputc(char c) {
  *REG32(0x10000000) = c;
}

int platform_dgetc(char *c, bool wait) {
  if (cbuf_read_char(&rxbuf, c, wait) == 1) return 0;
  else return -1;
}
