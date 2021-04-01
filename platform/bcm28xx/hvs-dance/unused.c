static uint8_t sdram_peek_fn[] = {
  0x02, 0xe8, 0x18, 0x00, 0xe0, 0x7e, // mov r2,0x7ee00018
  0x23, 0x08, // ld r3,(r2)     read SD_IDL
  0x03, 0x09, // st r3,(r0)     store to r0
  0x03, 0xe8, 0x30, 0x00, 0xe0, 0x7e, // mov r3,0x7ee00030
  0x34, 0x08, // ld r4,(r3)     read SD_CYL
  0x04, 0x31, // st r4,(r0+0x4) store to r0+4
  0x02, 0x32, // st r2,(r0+0x8) store SD_IDL addr to r0+8
  0x00, 0x60, // mov r0,0x0
  //0x20, 0x09, // st r0,(r2)     reset counters
  0x5a, 0x00, // rts
};

static uint32_t *mbox_property_start(int size) {
  size = ROUNDUP(size, CACHE_LINE);
  void *addr = memalign(CACHE_LINE, size);
  return addr;
}

static void mbox_property_send(uint32_t *msg, int size) {
  arch_clean_invalidate_cache_range((addr_t)msg, ROUNDUP(size, CACHE_LINE));

  mailbox_write(ch_propertytags_tovc, 0xc0000000 | vaddr_to_paddr(msg));

  uint32_t ack = 0x0;
  status_t ret = mailbox_read(ch_propertytags_tovc, &ack);
  assert(ret == NO_ERROR);
  ack = (ack & ~0xf) & 0x3fffffff;
  //printf("mailbox 0x%lx 0x%x\n", vaddr_to_paddr(msg), ack);
  assert(ack == vaddr_to_paddr(msg));

  arch_clean_invalidate_cache_range((addr_t)msg, ROUNDUP(size, CACHE_LINE));
  //printf("overall result: 0x%lx\n", msg[1]);
}

static uint32_t get_temp(void) {
  int size = 9*4;
  uint32_t *m = mbox_property_start(size);
  int i=0;

  m[i++] = 0; // size
  m[i++] = 0x00000000; // process request

  m[i++] = 0x30006; // Get temperature
  m[i++] = 8; // request size
  m[i++] = 0; // response size (out)
  m[i++] = 0; // temp id
  m[i++] = 0; // dummy

  m[i++] = 0; // end tag

  m[0] = i * 4;
  assert((i*4) < size);

  //for (int j=0; j<i; j++) printf("%d: 0x%08lx\n", j, m[j]);
  mbox_property_send(m, size);
  //for (int j=0; j<i; j++) printf("%d: 0x%08lx\n", j, m[j]);

  //printf("temp is %d\n", m[6]);
  uint32_t t = m[6];
  free(m);
  return t;
}

static void check_sdram_usage(void) {
  arch_clean_cache_range((addr_t)sdram_peek_fn, sizeof(sdram_peek_fn));

  int size = 52;
  size = ROUNDUP(size, CACHE_LINE);
  void *addr = memalign(CACHE_LINE, size);
  int size2 = ROUNDUP(4 * 3, CACHE_LINE);
  uint32_t *result = memalign(CACHE_LINE, size2);

  uint32_t *m = addr;
  int i=0;
  m[i++] = 0; // size
  m[i++] = 0x00000000; // process request

  m[i++] = 0x30010; // execute code
  m[i++] = 28; // value buffer
  m[i++] = 28;
  m[i++] = vaddr_to_paddr(sdram_peek_fn);
  m[i++] = vaddr_to_paddr(result) | 0xc0000000; // r0
  m[i++] = 0; // r1
  m[i++] = 0; // r2
  m[i++] = 0; // r3
  m[i++] = 0; // r4
  m[i++] = 0; // r5

  m[i++] = 0; // end tag
  m[0] = i * 4;
  assert((i*4) < size);

  arch_clean_invalidate_cache_range((addr_t)addr, size);
  arch_clean_invalidate_cache_range((addr_t)result, size2);

  mailbox_write(ch_propertytags_tovc, 0xc0000000 | vaddr_to_paddr(addr));

  uint32_t ack = 0x0;
  status_t ret = mailbox_read(ch_propertytags_tovc, &ack);
  assert(ret == NO_ERROR);
  ack = (ack & ~0xf) & 0x3fffffff;
  //printf("mailbox 0x%lx 0x%x\n", vaddr_to_paddr(addr), ack);
  assert(ack == vaddr_to_paddr(addr));

  arch_clean_invalidate_cache_range((addr_t)addr, size);
  arch_clean_invalidate_cache_range((addr_t)result, size2);

  m = addr;
  //printf("overall status: 0x%x safety 0x%x\n", m[1], result[2]);
  uint64_t idle = result[0];
  uint64_t cycles = result[1];
  uint32_t idle_percent = (idle * 100) / (cycles);
  uint32_t temp = get_temp();
  printf("with %d layers of %dx%d images, DDR2 was idle %d / %d cycles (%d%%), temp %d.%03d\n", sprite_limit, fb->width, fb->height, result[0], result[1], idle_percent, temp/1000, temp % 1000);

  free(addr);
  free(result);
}

#if 0
static void oldcheck_sdram_usage(void) {
  arch_clean_cache_range(sdram_peek_fn, sizeof(sdram_peek_fn));
  struct list_node list = LIST_INITIAL_VALUE(list);
  int ret = pmm_alloc_pages(1, &list);
  assert(ret == 1);
  paddr_t phys = vm_page_to_paddr(list.next);
  void *virt = paddr_to_kvaddr(phys);
  uint32_t *m = virt;
  int i=0;
  volatile uint32_t *result = (uint32_t*)(virt + 64);
  result[2] = 0xdeadbeef;
  m[i++] = 0; // size
  m[i++] = 0x00000000; // process request

  m[i++] = 0x30010; // execute code
  m[i++] = 28; // value buffer
  m[i++] = 28;
  m[i++] = vaddr_to_paddr(sdram_peek_fn);
  m[i++] = (phys + 64) | 0xc0000000; // r0
  m[i++] = 0; // r1
  m[i++] = 0; // r2
  m[i++] = 0; // r3
  m[i++] = 0; // r4
  m[i++] = 0; // r5

  m[i++] = 0; // end tag
  m[0] = i * 4;
  arch_clean_cache_range(virt, PAGE_SIZE);
  mailbox_write(ch_propertytags_tovc, 0xc0000000 | phys);

  uint32_t ack = 0x0;
  ret = mailbox_read(ch_propertytags_tovc, &ack);
  ack = (ack & ~0xf) & 0x3fffffff;
  //printf("mailbox 0x%x 0x%x\n", phys, ack);
  assert(ack == phys);
  arch_invalidate_cache_range(virt, PAGE_SIZE);
  m = virt;
  //printf("overall status: 0x%x safety 0x%x\n", m[1], result[2]);
  uint64_t idle = result[0];
  uint64_t cycles = result[1];
  uint32_t idle_percent = (idle * 100) / (cycles);
  printf("with %d layers of %dx%d images, DDR2 was idle %d / %d cycles (%d%%)\n", ITEMS, fb->width, fb->height, result[0], result[1], idle_percent);
  pmm_free(&list);
}
#endif

