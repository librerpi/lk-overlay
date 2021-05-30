#include <app.h>
#include <stdint.h>
#include <stdio.h>

struct test1 {
  union {
    uint32_t ints[16];
    float floats[16];
  };
};

const struct test1 value = { .floats = { 1.0, 2.0, 3.14, 0.123, 3.0, 0.1, 0.2, 0.3, 6.0 } };

static void dump_matrix(const char *name) {
  uint32_t matrix[16 * 16];
  asm volatile ("v32st HY(0++,0), (%0+=%1) REP16": :"r"(&matrix), "r"(4*16));
  printf("%s\n", name);
  for (int row=0; row<16; row++) {
    printf("row %d:", row);
    for (int col=0; col<16; col++) {
      printf("0x%08x ", matrix[(row*16) + col]);
    }
    puts("");
  }
}

static void float_init(const struct app_descriptor *app) {
  struct test1 arg1, arg2;
  for (int i=0; i<16; i++) {
    arg1.floats[i] = 2.0;
    arg2.floats[i] = 3.0;
  }
  arg1.floats[15] = 0;
  asm volatile ("v32ld HY(0,0), (%0)"::"r"(&arg1));
  asm volatile ("v32ld HY(1,0), (%0)"::"r"(&arg2));

  asm volatile ("v32and -, HY(0,0), %0 SETF": : "r"(0xff << 23));
  asm volatile ("v32and HY(2,0), HY(0,0), %0": : "r"(0x7fffff));
  asm volatile ("v32mov HY(4,0), %0": : "r"(0x800000));
  asm volatile ("v32or HY(2, 0), HY(2, 0), HY(4,0) IFNZ");

  asm volatile ("v32and -, HY(1,0), %0 SETF": : "r"(0xff << 23));
  asm volatile ("v32and HY(3,0), HY(1,0), %0": : "r"(0x7fffff));
  asm volatile ("v32mov HY(4,0), %0": : "r"(0x800000));
  asm volatile ("v32or HY(3, 0), HY(3, 0), HY(4,0) IFNZ");

  //asm volatile ("v32shl HY(2, 0), HY(2, 0), 8");
  //asm volatile ("v32shl HY(3, 0), HY(3, 0), 1");

  //asm volatile ("v32mov HY(2,0), %0": : "r"(0x12345678));
  //asm volatile ("v32mov HY(3,0), %0": : "r"(0x10001));
  asm volatile ("vmul32.uu HY(4,0), HX(2,0), HX(3,0)"); // part1, al * bl
  asm volatile ("vmul32.uu HY(5,0), HX(2,32), HX(3,0)"); // part2, ah * bl
  asm volatile ("vmul32.uu HY(6,0), HX(2,0), HX(3,0)+%0"::"r"(16)); // part3, al * bh
  asm volatile ("vmul32.uu HY(7,0), HX(2,0)+%0, HX(3,0)+%0"::"r"(16)); // part4, ah * bh
  dump_matrix("multed");
  asm volatile ("v32add HY(8,0), HY(5,0), HY(6,0)"); // partb, 47:16
  asm volatile ("v32add HY(9,0), HY(7,0), HX(8,0)");
  dump_matrix("added");

  volatile uint32_t part1=1, partb=2, part4=3;
  volatile uint64_t res = part1 + ((uint64_t)partb<<16) + (part4 << 32);

  for (int i=0; i<16; i++) {
    uint32_t fraction = value.ints[i] & 0x7fffff;
    printf("%d: 0x%x == %f sign:%d, exponent:%d, fraction:%d/0x%x\n", i, value.ints[i], value.floats[i], (value.ints[i] >> 31), (value.ints[i] >> 23) & 0xff, fraction, fraction);
  }
}

APP_START(float)
  .init = float_init,
  //.entry = float_entry,
APP_END
