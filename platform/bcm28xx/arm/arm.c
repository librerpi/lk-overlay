#include <app.h>
#include <assert.h>
#include <dev/gpio.h>
#include <kernel/timer.h>
#include <lib/cksum.h>
#include <libfdt.h>
#include <lk/init.h>
#include <lk/reg.h>
#include <platform/bcm28xx/a2w.h>
#include <platform/bcm28xx/arm.h>
#include <platform/bcm28xx/clock.h>
#include <platform/bcm28xx/cm.h>
#include <platform/bcm28xx/gpio.h>
#include <platform/bcm28xx/hvs.h>
#include <platform/bcm28xx/inter-arch.h>
#include <platform/bcm28xx/otp.h>
#include <platform/bcm28xx/pll.h>
#include <platform/bcm28xx/power.h>
#include <platform/bcm28xx/print_timestamp.h>
#include <platform/bcm28xx/udelay.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
  uint8_t *payload_addr;
  uint32_t payload_size;
} arm_payload;

extern arm_payload arm_payload_array[3];
static arm_payload *chosenPayload;
bool aarch64 = false;

timer_t arm_check;
uint32_t w = 620;
uint32_t h = 210;

// place arm framebuffer 96mb into ram
static const uint32_t fb_phys_addr = 96 * 1024 * 1024;

void mapBusToArm(uint32_t busAddr, uint32_t armAddr);
void setupClock(void);
void bridgeStart(bool cycleBrespBits);

typedef unsigned char v16b __attribute__((__vector_size__(16)));

#define logf(fmt, ...) { print_timestamp(); printf("[ARM:%s]: " fmt, __FUNCTION__, ##__VA_ARGS__); }

#define PM_PROC_ARMRSTN_CLR 0xffffffbf

static const uint8_t g_BrespTab[] = {
	0x10, 0x14, 0x10, 0x14, 0x10, 0x14, 0x10, 0x14, 0x10, 0x1C, 0x18, 0x1C, 0x18, 0x0,
	0x10, 0x14, 0x10, 0x1C, 0x10, 0x14, 0x10, 0x14, 0x10, 0x14, 0x10, 0x14, 0x10, 0x0,
	0x10, 0x14, 0x10, 0x1C, 0x18, 0x1C, 0x10, 0x14, 0x18, 0x1C, 0x10, 0x14, 0x10, 0x0,
	0x10, 0x14, 0x18, 0x14, 0x10, 0x14, 0x10, 0x14, 0x10, 0x14, 0x10, 0x14, 0x10, 0x0,
	0x10, 0x14, 0x18, 0x14, 0x18, 0x14, 0x10, 0x14, 0x10, 0x14, 0x10, 0x14, 0x18, 0x0
};

static void printregs(void) {
  printf("C0: 0x%x, C1: 0x%x, ERRHALT: 0x%x\n", *REG32(ARM_CONTROL0), *REG32(ARM_CONTROL1), *REG32(ARM_ERRHALT));
  printf("CM_LOCK: 0x%x\n", *REG32(CM_LOCK));
}

static enum handler_return arm_checker(struct timer *unused1, unsigned int unused2,  void *unused3) {
  static uint32_t last = 0;
  uint32_t current = *REG32(0xc0000100);
  if (last != current) printf("X changed 0x%x -> 0x%x, delta: %d\n", last, current, current-last);
  last = current;
  return INT_NO_RESCHEDULE;
}


static void setup_framebuffer(void) {
  int channel = 1;

  void *fb_addr_uncached = (void*)(0xc0000000 | fb_phys_addr);

  gfx_surface *simple_fb = gfx_create_surface(fb_addr_uncached, w, h, w, GFX_FORMAT_RGB_x888);
  gfx_fillrect(simple_fb, 0, 0, w, h, 0xff00ff00);
  hvs_layer *simple_fb_layer = malloc(sizeof(hvs_layer));
  mk_unity_layer(simple_fb_layer, simple_fb, 1000, 50, 30 + 210);
  //simple_fb_layer->w /= 4;
  //simple_fb_layer->h /= 4;
  simple_fb_layer->name = strdup("simple-framebuffer");

  mutex_acquire(&channels[channel].lock);
  hvs_dlist_add(channel, simple_fb_layer);
  hvs_update_dlist(channel);
  mutex_release(&channels[channel].lock);
}

#define checkerr if (ret < 0) { printf("%s():%d error %d %s\n", __FUNCTION__, __LINE__, ret, fdt_strerror(ret)); return NULL; }

static void *setupInterArchDtb(void) {
  size_t buffer_size = 1 * 1024 * 1024;
  void *v_fdt = malloc(buffer_size);
  int ret;

  ret = fdt_create(v_fdt, buffer_size);
  checkerr;
  //puts("a");

  ret = fdt_finish_reservemap(v_fdt);
  checkerr;
  //printf("b %d\n", fdt_size_dt_struct(v_fdt));

  ret = fdt_begin_node(v_fdt, "root");
  checkerr;
  //printf("c %d\n", fdt_size_dt_struct(v_fdt));


  {
    ret = fdt_begin_node(v_fdt, "framebuffer");
    checkerr;
    //printf("c2 %d\n", fdt_size_dt_struct(v_fdt));

    fdt_property_u32(v_fdt, "width", w);
    fdt_property_u32(v_fdt, "height", h);
    fdt_property_u32(v_fdt, "reg", fb_phys_addr);

    ret = fdt_end_node(v_fdt);
    checkerr;
  }

  {
    ret = fdt_begin_node(v_fdt, "timestamps");
    checkerr;

    fdt_property_u32(v_fdt, "3stage2_arch_init", arch_init_timestamp);
    fdt_property_u32(v_fdt, "4stage2_arm_start", *REG32(ST_CLO));

    ret = fdt_end_node(v_fdt);
    checkerr;
  }

  ret = fdt_end_node(v_fdt);
  checkerr;
  //puts("d");

  ret = fdt_finish(v_fdt);
  checkerr;
  //puts("e");

  return v_fdt;
}

static void enable_usb_host(void) {
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

  if (ethclk_pin > 0) {
    // GP1 routed to GPIO42 to drive ethernet/usb chip
    *REG32(CM_GP1CTL) = CM_PASSWORD | CM_GPnCTL_KILL_SET;
    while (*REG32(CM_GP1CTL) & CM_GPnCTL_BUSY_SET) {};

    *REG32(CM_GP1CTL) = CM_PASSWORD | (2 << CM_GPnCTL_MASH_LSB) | CM_SRC_PLLC_CORE0;
    *REG32(CM_GP1DIV) = CM_PASSWORD | 0x1147a; // divisor * 0x1000
    *REG32(CM_GP1CTL) = CM_PASSWORD | (2 << CM_GPnCTL_MASH_LSB) | CM_SRC_PLLC_CORE0 | CM_GPnCTL_ENAB_SET;

    gpio_config(ethclk_pin, kBCM2708Pinmux_ALT0);
  }

  if (lan_run > 0) {
    gpio_config(lan_run, kBCM2708PinmuxOut);
    gpio_set(lan_run, 0);
    udelay(1000);
    gpio_set(lan_run, 1);
  }
}

static void choose_arm_payload(void) {
  uint32_t revision = otp_read(30);
  uint32_t processor = (revision >> 12) & 0xf;
  assert(processor <= 2);
  chosenPayload = &arm_payload_array[processor];
  assert(chosenPayload->payload_addr);

  logf("detected a bcm%d, picking payload at %p size 0x%x\n", 2835 + processor, chosenPayload->payload_addr, chosenPayload->payload_size);
  if (processor == 2) aarch64 = true;
}

uint32_t orig_checksum;

static void copy_arm_payload(void) {
  void *original_start = chosenPayload->payload_addr;
  uint32_t size = chosenPayload->payload_size;

  void *dest = (void*)0xc0000000;

  memcpy(dest, original_start, size);
  uint32_t crc = crc32(0, original_start, size);
  uint32_t crc2 = crc32(0, dest, size);
  logf("checksums 0x%08x 0x%08x, size: %d\n", crc, crc2, size);

  logf("MEMORY: 0x0 + 0x%x: arm payload\n", chosenPayload->payload_size);
  orig_checksum = crc;
}

static void rechecksum_arm(void) {
  uint32_t *dest = (uint32_t*)0xc0000000;
  uint32_t size = chosenPayload->payload_size;
  uint32_t crc = crc32(0, (unsigned char*)dest, size);
  logf("checksum after: 0x%08x\n", crc);

  if (crc != orig_checksum) {
    logf("arm payload modified, its alive\n");
    uint32_t *orig = (uint32_t*)chosenPayload->payload_addr;
    for (unsigned int i=0; i<(chosenPayload->payload_size/4); i++) {
      if (orig[i] != dest[i]) logf("0x%x: 0x%x != 0x%x\n", i*4, orig[i], dest[i]);
    }
  }
}

static inter_core_header *find_header(uint32_t *start, uint32_t size) {
  for (uint32_t *i = start; i < (start + size); i += 4) { // increment by 16 bytes
    if (*i == INTER_ARCH_MAGIC) return (inter_core_header*)i;
  }
  return NULL;
}

static bool patch_arm_payload(void) {
  void *dtb_src = setupInterArchDtb();
  if (!dtb_src) return false;
  void *dtb_dst = (void*) ROUNDUP(chosenPayload->payload_size, 4);

  uint32_t t0 = *REG32(ST_CLO);
  inter_core_header *hdr = find_header((uint32_t*)0xc0000000, chosenPayload->payload_size);
  uint32_t t1 = *REG32(ST_CLO);
  if (hdr) {
    printf("header found at %p in %d uSec\n", hdr, t1-t0);
    printf("MEMORY: 0x0 + 0x%x: payload ram\n", hdr->end_of_ram);
    dtb_dst = (void*)(ROUNDUP(hdr->end_of_ram, 4));
  }

  int ret;
  size_t size = fdt_totalsize(dtb_src);

  ret = fdt_move(dtb_src, (void*)(0xc0000000 | (uint32_t)dtb_dst), size);
  checkerr;
  free(dtb_src);
  hdr->dtb_base = (uint32_t)dtb_dst;
  printf("MEMORY: %p + 0x%x: inter arch dtb\n", dtb_dst, size);
  return true;
}

#define PM_CAM1 0x7e100048
static void cam1_enable(void) {
  uint32_t t = *REG32(PM_CAM1);
  *REG32(PM_CAM1) = t | PM_PASSWORD | 0x1;
  t = *REG32(PM_CAM1);
  *REG32(PM_CAM1) = t | PM_PASSWORD | 0x4;
}

static void __attribute__(( optimize("-O1"))) arm_init(uint level) {
  bool jtag = true;

  choose_arm_payload();

  if (jtag) {
    gpio_config(22, kBCM2708Pinmux_ALT4);// TRST
    gpio_config(5, kBCM2708Pinmux_ALT5); // TDO
    gpio_config(13, kBCM2708Pinmux_ALT5); // TCK
    gpio_config(26, kBCM2708Pinmux_ALT4); // TDI
    gpio_config(12, kBCM2708Pinmux_ALT5); // TMS
  }

  timer_initialize(&arm_check);
  //timer_set_periodic(&arm_check, 1000, arm_checker, NULL);
  power_arm_start();
  printregs();
  printf("arm starting...\n");
  printf("CAM1_ICTL: 0x%x\n", *REG32(0x7e801100));

  cam1_enable();


  copy_arm_payload();
  patch_arm_payload();

  // first pass, map everything to the framebuffer, to act as a default
  for (int i=0; i<1024 ; i += 16) {
    mapBusToArm(0xc0000000 | fb_phys_addr, i * 1024 * 1024);
  }

  // second pass, map the lower 64mb as plain ram
  for (int i=0; i<64 ; i += 16) {
    mapBusToArm(0xc0000000 | (i * 1024 * 1024), i * 1024 * 1024);
  }

  for (int i=112; i<512 ; i += 16) {
    mapBusToArm(0xc0000000 | (i * 1024 * 1024), i * 1024 * 1024);
  }

  // add mmio
  mapBusToArm(0x7e000000, 0x20000000);
  mapBusToArm(0x7e000000, 0x3f000000);

  // add framebuffer
  mapBusToArm(0xc0000000 | fb_phys_addr, fb_phys_addr);

  logf("armid 0x%x, C0 0x%x\n", *REG32(ARM_ID), *REG32(ARM_CONTROL0));

  setup_framebuffer();
  enable_usb_host();
  cmd_hvs_dump_dlist(0, NULL);

  if (jtag) {
    *REG32(ARM_CONTROL0) |= ARM_C0_JTAGGPIO;
    logf("enabling jtag\n");
  }
  if (aarch64) {
    *REG32(ARM_CONTROL0) |= ARM_C0_AARCH64;
    logf("enabling aarch64\n");
  }
  /*
   * enable peripheral access, map arm secure bits to axi secure bits 1:1 and
   * set the mem size for who knows what reason.
   */
  *REG32(ARM_CONTROL0) |=
                  ARM_C0_BRESP2
                | ARM_C0_SIZ1G
                | ARM_C0_APROTPASS | ARM_C0_APROTMSK  // allow both kernel and userland to access mmio
                | ARM_C0_FULLPERI                     // allow access to all peripherals
                | (0x8 << 20)                         // ARM_C0_PRIO_PER
                | (0x5 << 24)                         // ARM_C0_PRIO_L2
                | (0xa << 28);                        // ARM_C0_PRIO_UC
  *REG32(ARM_CONTROL1) |= ARM_C1_PERSON;

  //printregs();

  setupClock();
  //printregs();
  power_arm_start();
  //printregs();
  bridgeStart(true);
  printregs();
}

// maps a 16mb chunk of ram
// the bus address has a resolution of 2mb
// the arm addr has a resolution of 16mb
// the entire mapping is 16mb long
// comments say there are 32 slots in the list (512mb mapped) an another 32 spare (1gig mapped)
void mapBusToArm(uint32_t busAddr, uint32_t armAddr) {
  volatile uint32_t* tte = REG32(ARM_TRANSLATE);

  uint32_t index = armAddr >> 24; // div by 16mb
  uint32_t pte = busAddr >> 21; // div by 2mb
  //printf("mapBusToArm(0x%x, 0x%x) index:%x, pte:%x\n", busAddr, armAddr, index, pte);

  tte[index] = pte;
}

void setupClock(void) {
  puts("initializing PLLB ...");

  /* oscillator->pllb */
  *REG32(A2W_XOSC_CTRL) |= A2W_PASSWORD | A2W_XOSC_CTRL_PLLBEN_SET;

  *REG32(A2W_PLLB_FRAC) = A2W_PASSWORD | 0x15555; // out of 0x100000
  *REG32(A2W_PLLB_CTRL) = A2W_PASSWORD | 52 | 0x1000;

  // sets clock to 19.2 * (52 + (0x15555 / 0x100000))
  // aka ~1000mhz

  *REG32(CM_PLLB) = CM_PASSWORD | CM_PLLB_DIGRST_SET | CM_PLLB_ANARST_SET;
  *REG32(CM_PLLB) = CM_PASSWORD | CM_PLLB_DIGRST_SET | CM_PLLB_ANARST_SET | CM_PLLB_HOLDARM_SET;

  *REG32(A2W_PLLB_ANA3) = A2W_PASSWORD | 0x100;
  *REG32(A2W_PLLB_ANA2) = A2W_PASSWORD | 0x0;
  *REG32(A2W_PLLB_ANA1) = A2W_PASSWORD | 0x140000;
  *REG32(A2W_PLLB_ANA0) = A2W_PASSWORD | 0x0;

  *REG32(CM_PLLB) = CM_PASSWORD | 0xAA; // hold all divider taps

  uint32_t dig0 = *REG32(A2W_PLLB_DIG0),
           dig1 = *REG32(A2W_PLLB_DIG1),
           dig2 = *REG32(A2W_PLLB_DIG2),
           dig3 = *REG32(A2W_PLLB_DIG3);

  *REG32(A2W_PLLB_DIG3) = A2W_PASSWORD | dig3;
  *REG32(A2W_PLLB_DIG2) = A2W_PASSWORD | (dig2 & 0xFFEFFBFE);
  *REG32(A2W_PLLB_DIG1) = A2W_PASSWORD | (dig1 & ~(1 << 14));
  *REG32(A2W_PLLB_DIG0) = A2W_PASSWORD | dig0;

  *REG32(A2W_PLLB_CTRL) |= A2W_PASSWORD | 0x20000;

  dig3 |= 0x42;

  *REG32(A2W_PLLB_DIG3) = A2W_PASSWORD | dig3;
  *REG32(A2W_PLLB_DIG2) = A2W_PASSWORD | dig2;
  *REG32(A2W_PLLB_DIG1) = A2W_PASSWORD | dig1;
  *REG32(A2W_PLLB_DIG0) = A2W_PASSWORD | dig0;


  *REG32(A2W_PLLB_ARM) = A2W_PASSWORD | 4;

  *REG32(CM_PLLB) = CM_PASSWORD | CM_PLLB_DIGRST_SET | CM_PLLB_ANARST_SET | CM_PLLB_HOLDARM_SET | CM_PLLB_LOADARM_SET;
  *REG32(CM_PLLB) = CM_PASSWORD | CM_PLLB_DIGRST_SET | CM_PLLB_ANARST_SET | CM_PLLB_HOLDARM_SET;
  *REG32(CM_PLLB) = CM_PASSWORD;

  const int src = 4;
  *REG32(CM_ARMCTL) = CM_PASSWORD | src | CM_ARMCTL_ENAB_SET;

  printf("KAIP  = 0x%X\n", *REG32(A2W_PLLB_ANA_KAIP)); /* 0x228 */
  printf("MULTI = 0x%X\n", *REG32(A2W_PLLB_ANA_MULTI)); /* 0x613277 */

  puts("ARM clock succesfully initialized!");
}

void bridgeWriteBresp(uint8_t bits) {
  //printf("bits: 0x%x\n", bits);
  *REG32(ARM_CONTROL0) = (*REG32(ARM_CONTROL0) & ~(ARM_C0_BRESP1|ARM_C0_BRESP2)) | bits;
  udelay(30);
}

void bridgeCycleBresp(void) {
  //puts("cycling through bresp bits ...");
  for (unsigned int i = 0; i < sizeof(g_BrespTab)/sizeof(g_BrespTab[0]); i++) {
    bridgeWriteBresp(g_BrespTab[i]);
  }
}

void bridgeStart(bool cycleBrespBits) {
  //puts("setting up async bridge ...");

  if (cycleBrespBits) {
    *REG32(PM_PROC) |= PM_PASSWORD | ~PM_PROC_ARMRSTN_CLR;
    bridgeCycleBresp();
    *REG32(PM_PROC) |= PM_PASSWORD | ~PM_PROC_ARMRSTN_CLR;

    udelay(300);
  }

  logf("starting async bridge now!\n");
  //udelay(6 * 1000 * 1000);

  *REG32(ARM_CONTROL1) &= ~ARM_C1_REQSTOP;


  if (!cycleBrespBits) {
    *REG32(PM_PROC) |= PM_PASSWORD | ~PM_PROC_ARMRSTN_CLR;
  }

  udelay(6 * 1000 * 1000);
  //puts("");

  //rechecksum_arm();
  logf("bridge init done, PM_PROC is now: 0x%X!\n", *REG32(PM_PROC));
}

LK_INIT_HOOK(arm, &arm_init, LK_INIT_LEVEL_PLATFORM + 10);
