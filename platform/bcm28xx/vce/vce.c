#include <string.h>
#include <limits.h>
#include <lk/console_cmd.h>
#include <lk/debug.h>
#include <lk/reg.h>
#include <lib/hexdump.h>
#include <platform/bcm28xx.h>
#include <platform/bcm28xx/cm.h>
#include <platform/bcm28xx/power.h>
#include <platform/bcm28xx/hvs.h>
#include <platform/bcm28xx/udelay.h>
#include <stdio.h>
#include <stdlib.h>

#define VCE_DATA_BASE   0x7f100000  // data RAM   (vce_loaddata target)
#define VCE_CODE_BASE   0x7f110000  // code RAM   (vce_loadprogram target)
#define VCE_REG_BASE    0x7f120000  // r0..r63, one word each (vce_setreg/getreg)
#define VCE_NUM_REGS    64

// Control block, from vce_run_start() / vce_run_complete() / vce_clear_interrupt().
#define VCE_CTL_BASE    0x7f140000
#define VCE_STATUS      (VCE_CTL_BASE + 0x00)  // run_complete reads this, bits 20:16
#define VCE_ENTRY       (VCE_CTL_BASE + 0x08)  // run_start writes the entry point here
#define VCE_STAT14      (VCE_CTL_BASE + 0x14)  // read by run_complete, meaning unknown
#define VCE_RUN         (VCE_CTL_BASE + 0x20)  // 0 = halt, 1 = go
#define VCE_IRQ_CLEAR   (VCE_CTL_BASE + 0x24)  // run_start writes 0xff; clear_interrupt writes bit31
#define VCE_IRQ_ENABLE  (VCE_CTL_BASE + 0x28)  // run_start writes 0x20 | (1 << n)
#define VCE_STAT30      (VCE_CTL_BASE + 0x30)  // read by run_complete, meaning unknown

#define DEAD_BUS        0x64627573  // "dbus" - clock/power domain gated off
#define DEAD_HDMI       0x68646d69  // "hdmi" - unmapped register hole in the HD block

#define VCE_NOP_INSN 0xe8fff000u   // or blank, blank, 0

static const char *deadness(uint32_t v) {
  if (v == DEAD_BUS) return " <- \"dbus\": clock/power domain is GATED OFF";
  if (v == DEAD_HDMI) return " <- \"hdmi\": unmapped register hole";
  return "";
}

static bool vce_alive(void) {
  for (int i = 0; i < VCE_NUM_REGS; i++) {
    uint32_t v = *REG32(VCE_REG_BASE + i * 4);
    if (v == DEAD_BUS || v == DEAD_HDMI) {
      printf("VCE is not responding: r%d reads 0x%08x%s\n", i, v, deadness(v));
      printf("try 'vce_clock 1' first\n");
      return false;
    }
  }
  return true;
}

#define ASB_H264_S_CTRL 0x7e00a018
#define ASB_H264_M_CTRL 0x7e00a01c
#define ASB_CLR_REQ     0x00000001
#define ASB_CLR_ACK     0x00000002

static bool asb_release(uint32_t reg, const char *name) {
  *REG32(reg) &= ~ASB_CLR_REQ;
  for (int i = 0; i < 100000; i++) {
    if ((*REG32(reg) & ASB_CLR_ACK) == 0) return true;
  }
  printf("TIMEOUT: %s CLR_ACK never cleared: 0x%08x\n", name, *REG32(reg));
  return false;
}

static bool vce_clock_running(void) {
  return (*REG32(CM_VCECTL) & CM_VCECTL_BUSY_SET) != 0;
}

// Bring the VCE far enough up to be addressable: clock first, then release both
// AXI ports. Every command calls this, so probing never touches a dead bus.
static bool vce_arm(bool verbose) {
  if (!vce_clock_running()) {
    if (verbose) printf("VCE clock is off, starting it\n");
    *REG32(CM_VCEDIV) = CM_PASSWORD | (4 << 12);
    *REG32(CM_VCECTL) = CM_PASSWORD | CM_SRC_OSC | CM_VCECTL_ENAB_SET;
    udelay(100);
    if (!vce_clock_running()) {
      printf("VCE clock did not start: CM_VCECTL=0x%08x\n", *REG32(CM_VCECTL));
      return false;
    }
  }

  if ((*REG32(ASB_H264_S_CTRL) & ASB_CLR_ACK) ||
      (*REG32(ASB_H264_M_CTRL) & ASB_CLR_ACK)) {
    if (verbose) printf("releasing the VCE AXI ports\n");
    if (!asb_release(ASB_H264_S_CTRL, "ASB_H264_S_CTRL")) return false;
    if (!asb_release(ASB_H264_M_CTRL, "ASB_H264_M_CTRL")) return false;
    udelay(100);
  }
  return true;
}

static int cmd_vce_clock(int argc, const console_cmd_args *argv) {
  bool on = (argc >= 2) ? (argv[1].u != 0) : true;

  printf("before: CM_VCECTL=0x%08x CM_VCEDIV=0x%08x S=0x%08x M=0x%08x\n",
         *REG32(CM_VCECTL), *REG32(CM_VCEDIV),
         *REG32(ASB_H264_S_CTRL), *REG32(ASB_H264_M_CTRL));

  if (on) {
    vce_arm(true);
  } else {
    // Hold the AXI ports before removing the clock, the reverse of vce_arm.
    *REG32(ASB_H264_M_CTRL) |= ASB_CLR_REQ;
    *REG32(ASB_H264_S_CTRL) |= ASB_CLR_REQ;
    *REG32(CM_VCECTL) = CM_PASSWORD | CM_VCECTL_KILL_SET;
    udelay(100);
  }

  printf("after:  CM_VCECTL=0x%08x CM_VCEDIV=0x%08x S=0x%08x M=0x%08x\n",
         *REG32(CM_VCECTL), *REG32(CM_VCEDIV),
         *REG32(ASB_H264_S_CTRL), *REG32(ASB_H264_M_CTRL));
  return 0;
}

// vce_dset <byte offset> <value> - write one word of data RAM.
static int cmd_vce_dset(int argc, const console_cmd_args *argv) {
  if (argc < 3) {
    printf("usage: %s <byte offset into data RAM> <value>\n", argv[0].str);
    return -1;
  }
  uint32_t off = argv[1].u & ~3u;
  if (off >= 0x800) {
    printf("offset out of range (data RAM is 2KB)\n");
    return -1;
  }
  if (!vce_arm(true)) return -1;
  *REG32(VCE_DATA_BASE + off) = argv[2].u;
  printf("data[0x%03x] = 0x%08x\n", off, *REG32(VCE_DATA_BASE + off));
  return 0;
}

static int cmd_vce_get(int argc, const console_cmd_args *argv) {
  if (argc < 2) {
    printf("usage: %s <reg 0-63>\n", argv[0].str);
    return -1;
  }
  uint32_t n = argv[1].u;
  if (n >= VCE_NUM_REGS) {
    printf("register out of range (0-%d)\n", VCE_NUM_REGS - 1);
    return -1;
  }
  if (!vce_arm(true)) return -1;

  uint32_t v = *REG32(VCE_REG_BASE + n * 4);
  printf("r%u = 0x%08x%s\n", n, v, deadness(v));
  return 0;
}

static int cmd_vce_set(int argc, const console_cmd_args *argv) {
  if (argc < 3) {
    printf("usage: %s <reg 0-63> <value>\n", argv[0].str);
    return -1;
  }
  uint32_t n = argv[1].u;
  if (n >= VCE_NUM_REGS) {
    printf("register out of range (0-%d)\n", VCE_NUM_REGS - 1);
    return -1;
  }
  if (!vce_arm(true)) return -1;

  *REG32(VCE_REG_BASE + n * 4) = argv[2].u;
  printf("r%u <- 0x%08x, reads back 0x%08x\n",
         n, (uint32_t)argv[2].u, *REG32(VCE_REG_BASE + n * 4));
  return 0;
}

static int cmd_vce_regs(int argc, const console_cmd_args *argv) {
  if (!vce_arm(true)) return -1;

  printf("VCE register file @ 0x%08x:\n", VCE_REG_BASE);
  hexdump_ram((void *)VCE_REG_BASE, VCE_REG_BASE, VCE_NUM_REGS * 4);
  return 0;
}

static int cmd_vce_ctl(int argc, const console_cmd_args *argv) {
  if (!vce_arm(true)) return -1;

  printf("VCE control block @ 0x%08x:\n", VCE_CTL_BASE);
  hexdump_ram((void *)VCE_CTL_BASE, VCE_CTL_BASE, 0x40);
  uint32_t st = *REG32(VCE_STATUS);
  printf("  status  0x%08x (code %u)%s\n", st, (st >> 16) & 0x1f, deadness(st));
  printf("  run     0x%08x\n", *REG32(VCE_RUN));
  printf("  irqen   0x%08x\n", *REG32(VCE_IRQ_ENABLE));
  return 0;
}

#define VCE_END_INSN 0x00fff000u   // end blank, blank, 0

static inline uint32_t vce_insn(unsigned op, unsigned rd, unsigned rs,
                                unsigned imm, unsigned cond) {
  return (op << 26) | (cond << 24) | ((rd & 0x3f) << 18) | ((rs & 0x3f) << 12) |
         (imm & 0xfff);
}

#define VCE_IRQ_SYNC 0x7e002008

static bool vce_run_program(uint32_t entry) {
  *REG32(VCE_RUN) = 0;
  *REG32(VCE_ENTRY) = entry;
  *REG32(VCE_IRQ_CLEAR) = 0xff;
  *REG32(VCE_IRQ_ENABLE) = 0x20;
  *REG32(VCE_RUN) = 1;

  bool started = false;
  for (int i = 0; i < 1000; i++) {
    if (*REG32(VCE_RUN) & 1) { started = true; break; }
  }

  bool finished = false;
  for (int i = 0; i < 100000; i++) {
    if ((*REG32(VCE_RUN) & 1) == 0) { finished = true; break; }
  }

  if (!started)
    finished = finished && ((*REG32(VCE_RUN) & 1) == 0);

  udelay(10);

  *REG32(VCE_RUN) = 0;

  *REG32(VCE_IRQ_CLEAR) = 0x80000000;
  for (int i = 0; i < 100000; i++) {
    if ((*REG32(VCE_IRQ_SYNC) & (1 << 4)) == 0) break;
  }
  *REG32(VCE_IRQ_CLEAR) = 1;
  return finished;
}

#define VCE_MAX_WIPE_WORDS 64
static void vce_wipe_code(unsigned words) {
  if (words > VCE_MAX_WIPE_WORDS) words = VCE_MAX_WIPE_WORDS;
  for (unsigned i = 0; i < words; i++)
    *REG32(VCE_CODE_BASE + i * 4) = VCE_END_INSN;
}

static bool vce_seed_via_prog(unsigned n, uint32_t val) {
  const uint32_t seed[] = { vce_insn(37, n, 63, val & 0xfff, 0), VCE_END_INSN };
  vce_wipe_code(8);
  for (unsigned i = 0; i < 2; i++)
    *REG32(VCE_CODE_BASE + i * 4) = seed[i];
  vce_run_program(0);
  uint32_t got = *REG32(VCE_REG_BASE + n * 4);
  if (got != (val & 0xfff)) {
    printf("seed program left r%u = 0x%08x, wanted 0x%08x\n", n, got,
           val & 0xfff);
    return false;
  }
  return true;
}

static int cmd_vce_exec(int argc, const console_cmd_args *argv) {
  if (argc < 3) {
    printf("usage: %s <entry> <insn0> [insn1 ...]\n", argv[0].str);
    return -1;
  }
  if (!vce_arm(true) || !vce_alive()) return -1;

  vce_wipe_code(64);
  unsigned n = argc - 2;
  for (unsigned i = 0; i < n; i++)
    *REG32(VCE_CODE_BASE + i * 4) = argv[2 + i].u;
  *REG32(VCE_CODE_BASE + n * 4) = VCE_END_INSN;

  vce_run_program(argv[1].u);

  uint32_t st = *REG32(VCE_STATUS);
  printf("status 0x%08x (end code %u)  CTL+0x30 0x%08x\n", st,
         (st >> 16) & 0x1f, *REG32(VCE_STAT30));
  hexdump_ram((void *)VCE_REG_BASE, VCE_REG_BASE, 16 * 4);
  return 0;
}

#include "vce_selftest.h"
#include "h264_mbloop_blob.h"

static bool vce_wipe_data(void) {
  if (!vce_seed_via_prog(3, 0x7ac))
    return false; // poison
  if (!vce_seed_via_prog(1, 0x400))
    return false; // first address

  uint32_t prog[8];
  prog[0] = vce_insn(58, 0, 3, 0, 0);        // or   last, r3, 0
  prog[1] = vce_insn(14, 0, 1, 0, 0);        // std  last, r1, 0
  prog[2] = vce_insn(60, 0, 1, 0x7fc, 0);    // cmp  last, r1, 0x7fc
  prog[3] = vce_insn(28, 63, 63, 0x14, 1);   // j.eq -> word 5
  prog[4] = vce_insn(44, 1, 1, 4, 0);        // add  r1, r1, 4   (delay slot)
  prog[5] = VCE_END_INSN;

  vce_wipe_code(12);
  for (unsigned i = 0; i < 6; i++)
    *REG32(VCE_CODE_BASE + i * 4) = prog[i];
  return vce_run_program(0);
}

__attribute__((section(".vce")))
int vce_demo(int x) {
  int t = 0;
  for (int i = 0; i < 4; i++)
    t += x >> i;
  return t;
}

static int vce_demo_reference(int x) {
  int t = 0;
  for (int i = 0; i < 4; i++)
    t += x >> i;
  return t;
}

extern const uint32_t vce_demo_code[] __asm__("vce_demo.vce");
extern const uint32_t vce_demo_nwords __asm__("vce_demo.vce.words");

static int cmd_vce_ckernel(int argc, const console_cmd_args *argv) {
  uint32_t x = (argc >= 2) ? argv[1].u : 8;
  if (!vce_arm(true) || !vce_alive()) return -1;

  unsigned n = vce_demo_nwords;
  printf("vce_demo: %u words of VCE code compiled from C in this file\n", n);
  if (n == 0 || n > VCE_MAX_WIPE_WORDS) {
    printf("implausible word count - is the .vce section reaching the link?\n");
    return -1;
  }

  if (x >= 0x800) {
    printf("input must be under 0x800 to be seedable\n");
    return -1;
  }
  if (!vce_seed_via_prog(1, x)) return -1;

  vce_wipe_code(n + 8);
  for (unsigned i = 0; i < n; i++)
    *REG32(VCE_CODE_BASE + i * 4) = vce_demo_code[i];

  bool fin = vce_run_program(0);
  uint32_t got = *REG32(VCE_REG_BASE + 1 * 4);
  uint32_t want = vce_demo_reference(x);

  printf("f(0x%x) = 0x%x, VC4 reference says 0x%x: %s%s\n", x, got, want,
         got == want ? "pass" : "FAIL", fin ? "" : " (did not finish)");
  if (got != want)
    printf("the two sides were built from the same C, so a mismatch is a "
           "codegen bug in one of the two backends\n");
  return got == want ? 0 : -1;
}

static unsigned vce_emit_u32(uint32_t *prog, unsigned n, unsigned reg,
                             uint32_t val) {
  prog[n++] = vce_insn(37, reg, 63, (val >> 22) & 0x3ff, 0);   // movi reg, top
  prog[n++] = vce_insn(32, reg, reg, 11, 0);                   // shl reg, 11
  prog[n++] = vce_insn(58, reg, reg, (val >> 11) & 0x7ff, 0);  // or  reg, mid
  prog[n++] = vce_insn(32, reg, reg, 11, 0);                   // shl reg, 11
  prog[n++] = vce_insn(58, reg, reg, val & 0x7ff, 0);          // or  reg, low
  return n;
}

static uint32_t vce_dma_src[64] __attribute__((aligned(64)));
static void vce_peek(uint32_t addr, uint32_t *out, unsigned count) {
  if (count > 4) count = 4;
  uint32_t prog[8];
  unsigned n = 0;
  for (unsigned i = 0; i < count; i++)
    prog[n++] = vce_insn(10, 20 + i, 63, addr + i * 4, 0);   // ldd r20+i
  prog[n++] = VCE_END_INSN;

  vce_wipe_code(16);
  for (unsigned i = 0; i < n; i++)
    *REG32(VCE_CODE_BASE + i * 4) = prog[i];
  vce_run_program(0);

  for (unsigned i = 0; i < count; i++)
    out[i] = *REG32(VCE_REG_BASE + (20 + i) * 4);
}

// The data-RAM windows a kernel and this code agree on.
#define VCE_PIPE_IN   0x100u
#define VCE_PIPE_OUT  0x200u
static uint32_t vce_pipe_buf[256] __attribute__((aligned(64)));

static bool vce_seed_u32(unsigned reg, uint32_t val) {
  uint32_t prog[8];
  unsigned n = vce_emit_u32(prog, 0, reg, val);
  prog[n++] = VCE_END_INSN;
  vce_wipe_code(16);
  for (unsigned i = 0; i < n; i++)
    *REG32(VCE_CODE_BASE + i * 4) = prog[i];
  if (!vce_run_program(0)) return false;
  uint32_t got = *REG32(VCE_REG_BASE + reg * 4);
  if (got != val) {
    printf("seeding r%u with 0x%x gave 0x%x\n", reg, val, got);
    return false;
  }
  return true;
}

#define VCE_DMA_WAIT 64

__attribute__((section(".vce")))
int vce_in_process(int n, uint32_t remote) {
  volatile int *in  = (volatile int *)VCE_PIPE_IN;
  volatile int *out = (volatile int *)VCE_PIPE_OUT;

  __asm__ volatile("dmafr  last, %0, 0" :: "r"(remote));
  __asm__ volatile("dmatdl last, %0, 0" :: "r"(VCE_PIPE_IN));
  __asm__ volatile("sendc  blank, %0, 0" :: "r"(n * 4));
  __asm__ volatile("dmarun blank, blank, 1" ::: "memory");
  for (int w = 0; w < VCE_DMA_WAIT; w++)
    __asm__ volatile("" ::: "memory");

  int total = 0;
  for (int i = 0; i < n; i++) {
    int v = in[i];
    out[i] = v * 3 + 1;
    total += v;
  }

  return total;
}

__attribute__((section(".vce")))
int vce_out(int n, uint32_t remote) {
  __asm__ volatile("dmato  last, %0, 0" :: "r"(remote));
  __asm__ volatile("dmafdl last, %0, 0" :: "r"(VCE_PIPE_OUT));
  __asm__ volatile("sendc  blank, %0, 0" :: "r"(n * 4));
  __asm__ volatile("dmarun blank, blank, 1" ::: "memory");
  for (int w = 0; w < VCE_DMA_WAIT; w++)
    __asm__ volatile("" ::: "memory");
  return n;
}

extern const uint32_t vce_in_process_code[] __asm__("vce_in_process.vce");
extern const uint32_t vce_in_process_nwords __asm__("vce_in_process.vce.words");
extern const uint32_t vce_out_code[] __asm__("vce_out.vce");
extern const uint32_t vce_out_nwords __asm__("vce_out.vce.words");

#define RDREG(a) ({ int _v;                                                   \
  __asm__ volatile("ver %0, blank, (" #a ")\n\t"                              \
                   "movi blank, blank, 0\n\t"                                 \
                   "movi blank, blank, 0\n\t"                                 \
                   "movi blank, blank, 0" : "=r"(_v)); _v; })

__attribute__((section(".vce")))
int vce_h264_regs(int n) {
  volatile int *out = (volatile int *)VCE_PIPE_OUT;
  out[0]  = RDREG(0x100); out[1]  = RDREG(0x104);
  out[2]  = RDREG(0x481); out[3]  = RDREG(0x720);
  out[4]  = RDREG(0x72c); out[5]  = RDREG(0x730);
  out[6]  = RDREG(0x740); out[7]  = RDREG(0xf5c);
  out[8]  = RDREG(0xf74); out[9]  = RDREG(0xf7c);
  out[10] = RDREG(0xfc0); out[11] = RDREG(0xfd0);
  return n;
}

extern const uint32_t vce_h264_regs_code[] __asm__("vce_h264_regs.vce");
extern const uint32_t vce_h264_regs_nwords __asm__("vce_h264_regs.vce.words");

// vce_h264regs - run it and print what the block reports.
static int cmd_vce_h264regs(int argc, const console_cmd_args *argv) {
  if (!vce_arm(true) || !vce_alive()) return -1;

  static const struct { uint32_t addr; const char *what; } map[] = {
    {0x100, "control"}, {0x104, "control, takes the mb info word"},
    {0x481, "control"}, {0x720, "control"},
    {0x72c, "control, written as v | v<<8"}, {0x730, "control, packed field"},
    {0x740, "control"}, {0xf5c, "status, read early"},
    {0xf74, "control, takes the mb info word"}, {0xf7c, "control, packed mode"},
    {0xfc0, "status"}, {0xfd0, "control, counter"},
  };

  // Poison, so a register that reads back as nothing is distinguishable from
  // one this kernel never reached.
  for (unsigned i = 0; i < 12; i++) {
    uint32_t p[] = { vce_insn(37, 0, 63, 0x777, 0),
                     vce_insn(14, 0, 63, VCE_PIPE_OUT + i * 4, 0), VCE_END_INSN };
    vce_wipe_code(8);
    for (unsigned k = 0; k < 3; k++) *REG32(VCE_CODE_BASE + k * 4) = p[k];
    vce_run_program(0);
  }

  if (!vce_seed_via_prog(1, 12)) return -1;
  vce_wipe_code(vce_h264_regs_nwords + 8);
  for (unsigned i = 0; i < vce_h264_regs_nwords; i++)
    *REG32(VCE_CODE_BASE + i * 4) = vce_h264_regs_code[i];
  bool fin = vce_run_program(0);

  uint32_t v[12];
  for (unsigned i = 0; i < 12; i += 4)
    vce_peek(VCE_PIPE_OUT + i * 4, &v[i], 4);

  printf("h264 codec block, %u words of kernel%s\n",
         (unsigned)vce_h264_regs_nwords, fin ? "" : " (did not finish)");
  printf("reg    value       role\n");
  for (unsigned i = 0; i < 12; i++)
    printf("0x%03x  0x%08x  %s%s\n", map[i].addr, v[i], map[i].what,
           v[i] == 0x777 ? "   <- kernel never wrote this slot" : "");
  return 0;
}

static int cmd_vce_condsweep(int argc, const console_cmd_args *argv) {
  if (!vce_arm(true) || !vce_alive()) return -1;

  printf("cmp/tst then j.<cond> blank, r23, 0x0c   (r23 = 0x40)\n");
  printf("test              cond  taken  not-taken  verdict\n");

  struct { const char *name; unsigned op; uint32_t val, opnd; bool expect_eq; } tests[] = {
    {"cmp r1=5 vs 5",  60, 5,   5,   true },
    {"cmp r1=5 vs 9",  60, 5,   9,   false},
    {"cmp r1=0 vs 0",  60, 0,   0,   true },
    {"tst r1=3 & 1",   61, 3,   1,   false},   // bits in common
    {"tst r1=2 & 1",   61, 2,   1,   true },   // no bits in common
  };

  for (unsigned t = 0; t < sizeof(tests)/sizeof(tests[0]); t++) {
    for (unsigned cond = 0; cond < 4; cond++) {
      // Clear both markers through the core - host register writes are dropped
      // once anything has run.
      for (unsigned k = 20; k <= 21; k++) {
        vce_wipe_code(4);
        *REG32(VCE_CODE_BASE + 0) = vce_insn(37, k, 63, 0, 0);
        *REG32(VCE_CODE_BASE + 4) = VCE_END_INSN;
        vce_run_program(0);
      }

      uint32_t prog[24];
      for (unsigned i = 0; i < 24; i++) prog[i] = vce_insn(37, 63, 63, 0, 0);
      prog[0]  = vce_insn(37, 23, 63, 0x40, 0);
      prog[1]  = vce_insn(37, 1, 63, tests[t].val & 0xfff, 0);
      prog[3]  = vce_insn(tests[t].op, 0, 1, tests[t].opnd & 0xfff, 0);
      prog[4]  = vce_insn(28, 63, 23, 0x0c, cond);
      prog[16] = vce_insn(37, 20, 63, 1, 0);   // 0x40, not taken
      prog[17] = VCE_END_INSN;
      prog[19] = vce_insn(37, 21, 63, 1, 0);   // 0x4c, taken
      prog[20] = VCE_END_INSN;

      vce_wipe_code(28);
      for (unsigned i = 0; i < 24; i++)
        *REG32(VCE_CODE_BASE + i * 4) = prog[i];
      bool fin = vce_run_program(0);

      uint32_t taken = *REG32(VCE_REG_BASE + 21 * 4);
      uint32_t nott  = *REG32(VCE_REG_BASE + 20 * 4);
      const char *v = (taken && !nott) ? "took the immediate"
                    : (nott && !taken) ? "landed on rs alone"
                    : (taken && nott)  ? "BOTH - ran twice?"
                                       : "neither - went elsewhere";
      printf("%-17s  %u     %u      %u          %s%s\n", tests[t].name, cond,
             !!taken, !!nott, v, fin ? "" : " (did not finish)");
    }
  }
  printf("a cond whose taken/not-taken flips with the compare is a real "
         "condition; one that is constant across a row is not\n");
  return 0;
}

#define VCE_STAGE   0x100u

__attribute__((section(".vce")))
int vce_pattern(int y, uint32_t remote, int x0n) {
  // x0n packs the starting pixel and the byte count: x0 in the low 16 bits,
  // count in the high 16. Three arguments are easy to seed, four are not.
  int x0 = x0n & 0xffff;
  int n  = (x0n >> 16) & 0xffff;
  volatile unsigned *buf = (volatile unsigned *)VCE_STAGE;
  for (int i = 0; i < n >> 2; i++) {
    int x = x0 + (i << 2);
    unsigned v0 = (unsigned)(((x + 0) ^ y) * 7) & 0xff;
    unsigned v1 = (unsigned)(((x + 1) ^ y) * 7) & 0xff;
    unsigned v2 = (unsigned)(((x + 2) ^ y) * 7) & 0xff;
    unsigned v3 = (unsigned)(((x + 3) ^ y) * 7) & 0xff;
    buf[i] = v0 | (v1 << 8) | (v2 << 16) | (v3 << 24);
  }
  __asm__ volatile("dmato  last, %0, 0" :: "r"(remote));
  __asm__ volatile("dmafdl last, %0, 0" :: "r"(VCE_STAGE));
  __asm__ volatile("sendc  blank, %0, 0" :: "r"(n));
  __asm__ volatile("dmarun blank, blank, 1" ::: "memory");
  for (int w = 0; w < VCE_DMA_WAIT; w++) __asm__ volatile("" ::: "memory");
  return y;
}

extern const uint32_t vce_pattern_code[]  __asm__("vce_pattern.vce");
extern const uint32_t vce_pattern_nwords  __asm__("vce_pattern.vce.words");
extern const uint32_t vce_pattern_pool[]  __asm__("vce_pattern.vce.pool");
extern const uint32_t vce_pattern_pooladdr __asm__("vce_pattern.vce.pool.addr");
extern const uint32_t vce_pattern_poolwords __asm__("vce_pattern.vce.pool.words");

// The YUV image the `yuv` app is displaying.  Writing into its luma plane is
// enough; the app re-uploads the display list on every vsync.
extern yuv_image_2plane *img;

// vce_display - fill the on-screen luma plane from the coprocessor.
static int cmd_vce_display(int argc, const console_cmd_args *argv) {
  if (!img || !img->luma) {
    printf("no YUV image - is the yuv app running?\n");
    return -1;
  }
  if (!vce_arm(true) || !vce_alive()) return -1;

  unsigned stride = img->luma_stride;
  unsigned rows = img->luma_size / stride;
  unsigned maxrows = (argc >= 2) ? argv[1].u : rows;
  if (maxrows > rows) maxrows = rows;

  uint32_t phys = (uint32_t)(uintptr_t)img->luma & 0x3fffffff;
  uint32_t base = phys | 0xc0000000;   // uncached, so the HVS sees what we wrote
  printf("luma %p (bus 0x%08x), %u x %u, kernel %u words, %u rows\n",
         img->luma, base, stride, rows, (unsigned)vce_pattern_nwords, maxrows);

  for (unsigned r = 0; r < maxrows; r++) {
    for (unsigned off = 0; off < stride; off += 256) {
      unsigned n = stride - off;
      if (n > 256) n = 256;
      n &= ~3u;                       // whole words only
      if (!n) break;

      // Restaged every run: data RAM has no loader, and this kernel reads its
      // shift constants out of the pool.
      for (unsigned k = 0; k < vce_pattern_poolwords; k++)
        *REG32(VCE_DATA_BASE + vce_pattern_pooladdr + k * 4) = vce_pattern_pool[k];

      if (!vce_seed_via_prog(1, r & 0xfff)) return -1;
      if (!vce_seed_u32(2, base + r * stride + off)) return -1;
      if (!vce_seed_u32(3, off | (n << 16))) return -1;

      vce_wipe_code(vce_pattern_nwords + 8);
      for (unsigned i = 0; i < vce_pattern_nwords; i++)
        *REG32(VCE_CODE_BASE + i * 4) = vce_pattern_code[i];
      if (!vce_run_program(0)) {
        printf("row %u chunk %u did not finish\n", r, off);
        return -1;
      }
    }
  }

  unsigned bad = 0;
  volatile uint8_t *l = (volatile uint8_t *)(uintptr_t)base;
  for (unsigned x = 0; x < (stride & ~3u); x++)
    if (l[x] != (uint8_t)(((x ^ 0) * 7) & 0xff)) bad++;
  printf("row 0: %u of %u pixels match%s\n", (stride & ~3u) - bad,
         stride & ~3u, bad ? "" : " - the picture is the VCE's");
  return bad ? -1 : 0;
}

static int cmd_vce_h264power(int argc, const console_cmd_args *argv) {
  uint32_t v = *REG32(PM_IMAGE);
  printf("PM_IMAGE = 0x%08x\n", v);
  printf("  POWUP  %d   POWOK  %d   ISPOW %d   ENABLE %d\n",
         !!(v & PM_POWUP), !!(v & PM_POWOK), !!(v & PM_ISPOW),
         !!(v & PM_ENABLE));
  printf("  H264RSTN %d   ISPRSTN %d   PERIRSTN %d\n",
         !!(v & 0x80), !!(v & 0x100), !!(v & 0x40));
  if (!(v & 0x80))
    printf("  -> the H.264 block is held in reset; its registers and buffers "
           "cannot be expected to mean anything yet\n");

  if (argc >= 2 && !strcmp(argv[1].str, "on")) {
    printf("bringing up the IMAGE domain\n");
    power_up_image();
    v = *REG32(PM_IMAGE);
    printf("PM_IMAGE = 0x%08x   H264RSTN %d\n", v, !!(v & 0x80));
    printf("now re-run vce_codereg: if the buffers stop reading as junk, the "
           "block was simply in reset\n");
  }
  return 0;
}

static int cmd_vce_codereg(int argc, const console_cmd_args *argv) {
  if (!vce_arm(true) || !vce_alive()) return -1;

  if (argc >= 4 && !strcmp(argv[1].str, "write")) {
    uint32_t a = argv[2].u & 0xfff, v = argv[3].u;
    uint32_t prog[8];
    unsigned n = 0;
    prog[n++] = vce_insn(37, 0, 63, v & 0xfff, 0);   // movi last, v
    prog[n++] = vce_insn(15, 0, 63, a, 0);           // op15 last, blank, (a)
    prog[n++] = vce_insn(37, 63, 63, 0, 0);
    prog[n++] = vce_insn(11, 20, 63, a, 0);          // read it back
    prog[n++] = vce_insn(37, 63, 63, 0, 0);
    prog[n++] = VCE_END_INSN;
    vce_wipe_code(12);
    for (unsigned k = 0; k < n; k++) *REG32(VCE_CODE_BASE + k * 4) = prog[k];
    bool fin = vce_run_program(0);
    printf("wrote 0x%x to 0x%03x, reads back 0x%08x%s\n", v & 0xfff, a,
           *REG32(VCE_REG_BASE + 20 * 4), fin ? "" : " (did not finish)");
    return 0;
  }

  static const uint32_t pages[] = {0x100, 0x110, 0x300, 0x400, 0x410, 0x420,
                                   0x430, 0x480, 0x500, 0x700, 0x720, 0x740};
  {
    uint32_t prog[32];
    unsigned n = 0;
    for (unsigned k = 0; k < 4; k++) {
      prog[n++] = vce_insn(11, 20 + k, 63, 0x400, 0);
      for (unsigned j = 0; j < 3; j++)
        prog[n++] = vce_insn(37, 63, 63, 0, 0);
    }
    prog[n++] = VCE_END_INSN;
    vce_wipe_code(36);
    for (unsigned k = 0; k < n; k++) *REG32(VCE_CODE_BASE + k * 4) = prog[k];
    vce_run_program(0);
    uint32_t a = *REG32(VCE_REG_BASE + 20*4), b = *REG32(VCE_REG_BASE + 21*4);
    uint32_t c = *REG32(VCE_REG_BASE + 22*4), d = *REG32(VCE_REG_BASE + 23*4);
    printf("0x400 read four times in one run: %08x %08x %08x %08x\n", a,b,c,d);
    printf("  -> %s\n", (a==b && b==c && c==d)
           ? "stable within a run, so it is real storage"
           : "differs within a single run - nothing is driving these lines, so "
             "that region is not powered or clocked yet");
  }

  printf("\nop11 window (read only), hex and ascii:\n");
  for (unsigned pi = 0; pi < sizeof(pages)/sizeof(pages[0]); pi++) {
    uint32_t base = pages[pi];
    uint32_t prog[32];
    unsigned n = 0;
    prog[n++] = vce_insn(11, 19, 63, 0x480, 0);
    for (unsigned k = 0; k < 3; k++)
      prog[n++] = vce_insn(37, 63, 63, 0, 0);
    for (unsigned k = 0; k < 4; k++) {
      prog[n++] = vce_insn(11, 20 + k, 63, base + k * 4, 0);
      for (unsigned j = 0; j < 3; j++)
        prog[n++] = vce_insn(37, 63, 63, 0, 0);    // movi blank - settle
    }
    prog[n++] = VCE_END_INSN;
    vce_wipe_code(36);
    for (unsigned k = 0; k < n; k++) *REG32(VCE_CODE_BASE + k * 4) = prog[k];
    vce_run_program(0);

    uint32_t id = *REG32(VCE_REG_BASE + 19 * 4);
    char txt[17];
    printf("  0x%03x ", base);
    for (unsigned k = 0; k < 4; k++) {
      uint32_t v = *REG32(VCE_REG_BASE + (20 + k) * 4);
      printf("%08x ", v);
      for (unsigned b = 0; b < 4; b++) {
        char c = (v >> (8 * b)) & 0xff;
        txt[k * 4 + b] = (c >= 32 && c < 127) ? c : '.';
      }
    }
    txt[16] = 0;
    printf(" |%s|%s\n", txt,
           id == 0x68323634 ? "" : "  <- ID check FAILED, row is misaligned");
  }
  return 0;
}

static int cmd_vce_selfdma(int argc, const console_cmd_args *argv) {
  unsigned n = (argc >= 2) ? argv[1].u : 8;
  if (n > 32) n = 32;
  if (!vce_arm(true) || !vce_alive()) return -1;

  uint32_t phys = (uint32_t)(uintptr_t)vce_pipe_buf & 0x3fffffff;
  uint32_t remote = phys | 0xc0000000;   // uncached alias: really in memory
  volatile uint32_t *buf = (volatile uint32_t *)(uintptr_t)remote;

  for (unsigned i = 0; i < n; i++)
    buf[i] = i * 5 + 2;
  uint32_t want_total = 0;
  for (unsigned i = 0; i < n; i++)
    want_total += buf[i];

  // Poison both windows through the core. Data RAM persists across runs, so
  // without this a transfer that never happened is indistinguishable from one
  // that did - and host writes do not reliably reach what the core reads here,
  // which is why the poison goes through a program rather than a store.
  for (unsigned i = 0; i < n; i++) {
    uint32_t p[4];
    unsigned m = 0;
    p[m++] = vce_insn(37, 0, 63, 0x666, 0);
    p[m++] = vce_insn(14, 0, 63, VCE_PIPE_IN + i * 4, 0);
    p[m++] = vce_insn(14, 0, 63, VCE_PIPE_OUT + i * 4, 0);
    p[m++] = VCE_END_INSN;
    vce_wipe_code(8);
    for (unsigned k = 0; k < m; k++) *REG32(VCE_CODE_BASE + k * 4) = p[k];
    vce_run_program(0);
  }

  printf("kernels: %u + %u words, %u elements, buffer at 0x%08x\n",
         (unsigned)vce_in_process_nwords, (unsigned)vce_out_nwords, n, remote);

  // r1 = n, r2 = the host buffer address. r2 needs a full 32 bits, which one
  // `movi` cannot carry.
  if (!vce_seed_via_prog(1, n)) return -1;
  if (!vce_seed_u32(2, remote)) return -1;

  vce_wipe_code(vce_in_process_nwords + 8);
  for (unsigned i = 0; i < vce_in_process_nwords; i++)
    *REG32(VCE_CODE_BASE + i * 4) = vce_in_process_code[i];
  bool fin = vce_run_program(0);
  uint32_t got_total = *REG32(VCE_REG_BASE + 1 * 4);

  // Second run: push the results out. See vce_out for why this cannot be part
  // of the run above.
  if (!vce_seed_via_prog(1, n)) return -1;
  if (!vce_seed_u32(2, remote)) return -1;
  vce_wipe_code(vce_out_nwords + 8);
  for (unsigned i = 0; i < vce_out_nwords; i++)
    *REG32(VCE_CODE_BASE + i * 4) = vce_out_code[i];
  fin = vce_run_program(0) && fin;

  unsigned bad = 0;
  for (unsigned i = 0; i < n; i++) {
    uint32_t want = (i * 5 + 2) * 3 + 1;
    if (buf[i] != want) {
      if (bad < 4)
        printf("  [%u] got 0x%x want 0x%x\n", i, buf[i], want);
      bad++;
    }
  }

  printf("sum: got %u want %u %s\n", got_total, want_total,
         got_total == want_total ? "ok" : "WRONG");
  printf("elements: %u of %u correct%s\n", n - bad, n,
         fin ? "" : " (did not finish)");

  // Attribute a failure: the two halves fail in distinguishable ways.
  if (got_total == 0x666u * n)
    printf("the sum is n copies of the poison: the inbound transfer had not "
           "landed when the loop ran\n");
  else if (!bad && got_total == want_total && fin)
    printf("the kernels drive their own DMA: one run pulls the input in and "
           "processes it, the next pushes the results out\n");
  else if (got_total == want_total) {
    uint32_t peek[4];
    vce_peek(VCE_PIPE_OUT, peek, 4);
    printf("the kernel computed correctly and left 0x%x 0x%x 0x%x 0x%x at "
           "data[0x%x], so the outbound transfer is at fault\n",
           peek[0], peek[1], peek[2], peek[3], VCE_PIPE_OUT);
  }
  return (bad || got_total != want_total) ? -1 : 0;
}

static int cmd_vce_selftest(int argc, const console_cmd_args *argv) {
  if (!vce_arm(true) || !vce_alive()) return -1;

  const unsigned N = sizeof(vce_selftests) / sizeof(vce_selftests[0]);
  printf("case          entry        in        want      got       wds sum      "
         "result\n");

  unsigned pass = 0, run = 0;
  for (unsigned i = 0; i < N; i++) {
    const struct vce_selftest_case *tc = &vce_selftests[i];

    // Reset the shared data region first, so a case cannot inherit the last
    // one's scratch or frame contents.  Before this, which case failed depended
    // on the order they ran in.
    if (!vce_wipe_data()) {
      printf("%-13s data wipe failed - stopping after %u cases\n", tc->name,
             run);
      break;
    }

    if (!vce_seed_via_prog(1, tc->input)) {
      printf("%-13s seeding failed - stopping after %u cases\n", tc->name, run);
      break;
    }

    vce_wipe_code(tc->words + 8);
    for (unsigned k = 0; k < tc->words; k++)
      *REG32(VCE_CODE_BASE + k * 4) = tc->prog[k];

    for (unsigned k = 0; k < tc->pool_words; k++) {
      *REG32(VCE_DATA_BASE + VCE_POOL_BASE + k * 4) = tc->pool[k];
      *REG32(VCE_DATA_BASE + (VCE_POOL_BASE | 0x800) + k * 4) = tc->pool[k];
    }

    bool fin = vce_run_program(tc->entry);
    uint32_t got = *REG32(VCE_REG_BASE + 1 * 4);
    bool ok = fin && got == tc->expect;
    uint32_t sum = 0;
    for (unsigned k = 0; k < tc->words; k++)
      sum += *REG32(VCE_CODE_BASE + k * 4);

    printf("%-13s %-12s 0x%-7x 0x%-7x 0x%-7x %-3u 0x%08x %s\n", tc->name,
           tc->entry_sym, tc->input, tc->expect, got, tc->words, sum,
           ok ? "pass" : (fin ? "FAIL" : "DID NOT FINISH"));

    if (!ok) {
      vce_seed_via_prog(1, tc->input);
      vce_wipe_code(tc->words + 8);
      for (unsigned k = 0; k < tc->words; k++)
        *REG32(VCE_CODE_BASE + k * 4) = tc->prog[k];
      for (unsigned k = 0; k < tc->pool_words; k++) {
        *REG32(VCE_DATA_BASE + VCE_POOL_BASE + k * 4) = tc->pool[k];
        *REG32(VCE_DATA_BASE + (VCE_POOL_BASE | 0x800) + k * 4) = tc->pool[k];
      }
      bool fin2 = vce_run_program(tc->entry);
      uint32_t got2 = *REG32(VCE_REG_BASE + 1 * 4);
      printf("              retry: got 0x%x %s%s\n", got2,
             got2 == tc->expect ? "PASS - state-dependent, not a miscompile"
                                : "FAIL again - a real miscompile",
             fin2 ? "" : " (did not finish)");
    }

    run++;
    if (ok) pass++;
  }

  printf("%u/%u passed\n", pass, run);
  if (pass == run && run == N)
    printf("compiled output runs correctly on hardware\n");
  else
    printf("a failure here is a real miscompile - the programs are the "
           "compiler's own output, not a transcription of it\n");
  return 0;
}

#define H264_STOP_DEFAULT 312

// Load `bytes` from host memory into VCE data RAM at `local`. One transfer per
// run: a loop of transfers inside a single kernel delivers nothing, not even
// its first chunk, which is measured and still unexplained.
static bool vce_dma_into(uint32_t local, const uint32_t *src, unsigned bytes) {
  while (bytes) {
    unsigned n = bytes > 256 ? 256 : bytes;
    memcpy(vce_dma_src, src, n);

    uint32_t prog[32];
    unsigned k = 0;
    k = vce_emit_u32(prog, k, 1, (uint32_t)vce_dma_src | 0xc0000000u);
    k = vce_emit_u32(prog, k, 2, local);
    k = vce_emit_u32(prog, k, 3, n);
    prog[k++] = vce_insn(16, 0, 1, 0, 0);    // dmafr  last, r1  - remote
    prog[k++] = vce_insn(18, 0, 2, 0, 0);    // dmatdl last, r2  - local
    prog[k++] = vce_insn(2, 63, 3, 0, 0);    // sendc  blank, r3 - length
    prog[k++] = vce_insn(3, 63, 63, 1, 0);   // dmarun
    for (int w = 0; w < 8; w++) prog[k++] = VCE_NOP_INSN;
    prog[k++] = VCE_END_INSN;

    for (unsigned i = 0; i < k; i++)
      *REG32(VCE_CODE_BASE + i * 4) = prog[i];
    if (!vce_run_program(0)) {
      printf("DMA of %u bytes to data %#x did not complete\n", n, local);
      return false;
    }

    src += n / 4;
    local += n;
    bytes -= n;
  }
  return true;
}

// Read codec registers through the core.  `ver` has three instructions of
// result latency, so each read is padded before the next one starts - an
// asm-based probe that forgot this read every register one slot late.
static void vce_creg_read(const uint32_t *addrs, unsigned count,
                          uint32_t *out) {
  if (count > 8) count = 8;
  uint32_t prog[64];
  unsigned k = 0;
  for (unsigned i = 0; i < count; i++) {
    prog[k++] = vce_insn(11, 20 + i, 63, addrs[i], 0);   // ver r20+i, (addr)
    prog[k++] = vce_insn(37, 63, 63, 0, 0);              // movi blank - settle
    prog[k++] = vce_insn(37, 63, 63, 0, 0);
    prog[k++] = vce_insn(37, 63, 63, 0, 0);
  }
  prog[k++] = VCE_END_INSN;

  for (unsigned i = 0; i < k; i++)
    *REG32(VCE_CODE_BASE + i * 4) = prog[i];
  vce_run_program(0);

  for (unsigned i = 0; i < count; i++)
    out[i] = *REG32(VCE_REG_BASE + (20 + i) * 4);
}

// The registers the blob itself reads, so they are known to be readable rather
// than guessed at.  0xf5c and 0xfc0 are the two it consults during init.
static const uint32_t h264_watch[] = {
  0xf5c, 0xfc0, 0x80d, 0x811, 0x815, 0x819, 0x829, 0x835,
};
#define H264_NWATCH (sizeof(h264_watch) / sizeof(h264_watch[0]))

static int cmd_vce_h264load(int argc, const console_cmd_args *argv) {
  unsigned stop = argc > 1 ? argv[1].u : H264_STOP_DEFAULT;
  if (stop > countof(h264_mbloop_code)) {
    printf("stop word %u is past the end of the blob (%u words)\n",
           stop, (unsigned)countof(h264_mbloop_code));
    return -1;
  }
  if (!vce_arm(true) || !vce_alive()) return -1;

  uint32_t before[H264_NWATCH], after[H264_NWATCH];
  vce_creg_read(h264_watch, H264_NWATCH, before);

  printf("staging data: %u bytes to data RAM 0\n",
         (unsigned)sizeof(h264_mbloop_data));
  if (!vce_dma_into(0, h264_mbloop_data, sizeof(h264_mbloop_data)))
    return -1;

  // Read a few words back through the core rather than trusting the DMA.  Host
  // reads of the data window do not reliably see what the core sees, so this
  // goes through `ldd` like everything else that matters.
  static const uint32_t check[] = { 0x000, 0x400, 0x61c, 0xc40 };
  bool ok = true;
  for (unsigned i = 0; i < countof(check); i++) {
    uint32_t got;
    vce_peek(check[i], &got, 1);
    uint32_t want = h264_mbloop_data[check[i] / 4];
    printf("  data %#05x = %08x  want %08x  %s\n", check[i], got, want,
           got == want ? "ok" : "MISMATCH");
    if (got != want) ok = false;
  }
  if (!ok) {
    printf("data staging failed - not running the blob\n");
    return -1;
  }

  printf("loading code: %u words, halting at word %u\n",
         (unsigned)countof(h264_mbloop_code), stop);
  for (unsigned i = 0; i < countof(h264_mbloop_code); i++)
    *REG32(VCE_CODE_BASE + i * 4) =
        (i == stop) ? VCE_END_INSN : h264_mbloop_code[i];

  bool finished = vce_run_program(H264_MBLOOP_ENTRY);
  printf("run %s\n", finished ? "completed" : "DID NOT COMPLETE");

  vce_creg_read(h264_watch, H264_NWATCH, after);
  printf("codec registers, idle -> after init:\n");
  for (unsigned i = 0; i < H264_NWATCH; i++)
    printf("  %#05x  %08x -> %08x  %s\n", h264_watch[i], before[i], after[i],
           before[i] == after[i] ? "" : "CHANGED");
  return 0;
}

STATIC_COMMAND_START
STATIC_COMMAND("vce_clock", "enable/disable the VCE clock: vce_clock <0|1> [div]", &cmd_vce_clock)
STATIC_COMMAND("vce_dset", "write one word of VCE data RAM", &cmd_vce_dset)
STATIC_COMMAND("vce_exec", "load and run microcode: vce_exec <entry> <insn>...", &cmd_vce_exec)
STATIC_COMMAND("vce_ckernel", "run a VCE kernel written in C in this file: vce_ckernel [x]", &cmd_vce_ckernel)
STATIC_COMMAND("vce_h264power", "is the H.264 block powered: vce_h264power [on]", &cmd_vce_h264power)
STATIC_COMMAND("vce_codereg", "probe the codec-block register window (opcodes 11/15)", &cmd_vce_codereg)
STATIC_COMMAND("vce_h264regs", "read the h264 codec block control registers", &cmd_vce_h264regs)
STATIC_COMMAND("vce_condsweep", "what cond 2 and 3 select on a branch", &cmd_vce_condsweep)
STATIC_COMMAND("vce_display", "the VCE computes an image and DMAs it to the screen", &cmd_vce_display)
STATIC_COMMAND("vce_selfdma", "kernel drives its own DMA, one run: vce_selfdma [n]", &cmd_vce_selfdma)
STATIC_COMMAND("vce_h264load", "stage and run the stock h264_mbloop blob: vce_h264load [stopword]", &cmd_vce_h264load)
STATIC_COMMAND("vce_selftest", "run the compiler's own output on the core", &cmd_vce_selftest)
STATIC_COMMAND("vce_regs", "hexdump the VCE register file", &cmd_vce_regs)
STATIC_COMMAND("vce_ctl", "hexdump the VCE control block", &cmd_vce_ctl)
STATIC_COMMAND("vce_get", "read one VCE register", &cmd_vce_get)
STATIC_COMMAND("vce_set", "write one VCE register", &cmd_vce_set)
STATIC_COMMAND_END(vce);
