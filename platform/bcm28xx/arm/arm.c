#include <app.h>
#include <dev/gpio.h>
#include <kernel/timer.h>
#include <lib/cksum.h>
#include <lk/init.h>
#include <lk/reg.h>
#include <platform/bcm28xx/a2w.h>
#include <platform/bcm28xx/arm.h>
#include <platform/bcm28xx/cm.h>
#include <platform/bcm28xx/gpio.h>
#include <platform/bcm28xx/hvs.h>
#include <platform/bcm28xx/otp.h>
#include <platform/bcm28xx/pll.h>
#include <platform/bcm28xx/power.h>
#include <platform/bcm28xx/udelay.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern uint8_t arm_payload_start, arm_payload_end;
timer_t arm_check;

void mapBusToArm(uint32_t busAddr, uint32_t armAddr);
void setupClock(void);
void bridgeStart(bool cycleBrespBits);


typedef unsigned char v16b __attribute__((__vector_size__(16)));

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
  int w = 620;
  int h = 210;

  gfx_surface *simple_fb = gfx_create_surface(0xc0000000 | (128 * 1024 * 1024), w, h, w, GFX_FORMAT_ARGB_8888);
  gfx_fillrect(simple_fb, 0, 0, w, h, 0xff00ff00);
  hvs_layer *simple_fb_layer = malloc(sizeof(hvs_layer));
  MK_UNITY_LAYER(simple_fb_layer, simple_fb, 1000, 50, 30 + 210);
  //simple_fb_layer->w /= 4;
  //simple_fb_layer->h /= 4;
  simple_fb_layer->name = "simple-framebuffer";

  mutex_acquire(&channels[channel].lock);
  hvs_dlist_add(channel, simple_fb_layer);
  hvs_update_dlist(channel);
  mutex_release(&channels[channel].lock);
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

static void __attribute__(( optimize("-O1"))) arm_init(uint level) {
  bool jtag = true;

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
  void *original_start = &arm_payload_start;
  double a = ((double)(int)original_start) * 1.3;
  printf("arm starting... %f\n", a);
  uint32_t size = &arm_payload_end - &arm_payload_start;
  memcpy((void*)0xc0000000, original_start, size);
  uint32_t crc = crc32(0, original_start, size);
  uint32_t crc2 = crc32(0, 0xc0000000, size);
  printf("checksums 0x%08x 0x%08x\n", crc, crc2);

  // first pass, map everything to the framebuffer, to act as a default
  for (int i=0; i<1024 ; i += 16) {
    mapBusToArm(0xc8000000, i * 1024 * 1024);
  }

  // second pass, map the lower 64mb as plain ram
  for (int i=0; i<64 ; i += 16) {
    mapBusToArm(0xc0000000 | (i * 1024 * 1024), i * 1024 * 1024);
  }

  // add mmio
  mapBusToArm(0x7e000000, 0x20000000);
  mapBusToArm(0x7e000000, 0x3f000000);

  // add framebuffer
  mapBusToArm(0xc8000000, 0x08000000);

  printf("armid 0x%x, C0 0x%x\n", *REG32(ARM_ID), *REG32(ARM_CONTROL0));

  setup_framebuffer();
  enable_usb_host();
  cmd_hvs_dump_dlist(0, NULL);

  if (jtag) {
    *REG32(ARM_CONTROL0) |= ARM_C0_JTAGGPIO;
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
  // | ARM_C0_AARCH64;
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
  printf("mapBusToArm(0x%x, 0x%x) index:%x, pte:%x\n", busAddr, armAddr, index, pte);

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

  *REG32(CM_ARMCTL) = CM_PASSWORD | 4 | CM_ARMCTL_ENAB_SET;

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

  puts("starting async bridge now!");
  udelay(1000000);
  *REG32(ARM_CONTROL1) &= ~ARM_C1_REQSTOP;

  if (!cycleBrespBits) {
    *REG32(PM_PROC) |= PM_PASSWORD | ~PM_PROC_ARMRSTN_CLR;
  }

  udelay(6 * 1000 * 1000);
  printf("\nbridge init done, PM_PROC is now: 0x%X!\n", *REG32(PM_PROC));
}

LK_INIT_HOOK(arm, &arm_init, LK_INIT_LEVEL_PLATFORM + 1);
