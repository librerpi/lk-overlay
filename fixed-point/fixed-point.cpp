#include <stdint.h>
#include <inttypes.h>
#include <iostream>

#include "fixed-point.h"
#include "util.h"

using namespace std;

#define MHz 1000000

template<typename storage, int integer, int fraction> ostream& operator<<(ostream &os, Fixed<storage,integer,fraction> &num) {
  Fixed<uint64_t,integer,fraction> bigger = num.template expand<uint64_t>();

  char buffer[64];
  snprintf(buffer, 63, "%lu/0x%lx", (int64_t)bigger.getIntger(), (int64_t)bigger.getIntger());

  if (fraction == 0) {
    return os << "Fixed<storage," << integer << "," << fraction << "> " << (uint64_t)num.s << "/" << buffer;
  } else if (fraction > 0) {
    return os << "Fixed<storage," << integer << "," << fraction << "> " << bigger.getFloat() << " (" << num.s << ")";
  } else {
    return os << "Fixed<storage," << integer << "," << fraction << "> " << bigger.getFloat() << "/" << buffer << " (" << num.s << ")";
  }
}

void checkPll(uint32_t xtal, uint32_t goal) {
  Fixed<uint32_t,12,20> divisor = computePllDivisor(xtal/1000, goal/1000);
  printf("%0.2fMHz * %0.3f (0x%" PRIx32 ") == %0.2fMHz\n", (double)xtal/1000/1000, (double)divisor.s / 0x100000, divisor.s, xtal * ((double)divisor.s / 0x100000) / 1000 / 1000);
  printf("divisor %" PRIu32 " + (%" PRIu32 " / 0x100000)\n", divisor.getIntger(), divisor.getFraction());
}

void checkPL011(uint32_t refclk_in, uint32_t baud) {
  Fixed<uint32_t,16,6> divisor = computePL011Divisor(refclk_in, baud);
  printf("IBRD = %" PRIu32 ", FBRD = %" PRIu32 ", %0.2f MHz / %0.3f / 16 == %d\n", divisor.getIntger(), divisor.getFraction(), (double)refclk_in / MHz, divisor.getFloat(), (int)floor((double)refclk_in / divisor.getFloat() / 16));
}

uint64_t mulh_long( int rs1, int rs2 ) {
  uint32_t rs1lo = ( rs1 & 0xffff );
  uint32_t rs2lo = ( rs2 & 0xffff );
  uint32_t rs1hi = ( rs1 >> 16 );
  uint32_t rs2hi = ( rs2 >> 16 );

  uint32_t lowpart = rs1lo * rs2lo;
  int32_t mid1 = rs1hi * rs2lo;
  int32_t mid2 = rs1lo * rs2hi;
  uint32_t highpart = rs1hi * rs2hi;

  int64_t part1 = (uint64_t)highpart << 32;
  int64_t part2 = lowpart;
  int64_t part3 = (uint64_t)mid1 << 16;
  int64_t part4 = (uint64_t)mid2 << 16;
  uint64_t product  = part1 + part2 + part3 + part4;

  printf("rs1lo: 0x%x\n", rs1lo);
  printf("rs1hi: 0x%x\n", rs1hi);
  printf("rs2lo: 0x%x\n", rs2lo);
  printf("rs2hi: 0x%x\n", rs2hi);

  printf("mid1: 0x%x\n", mid1);

  printf("part1 %ld / 0x%lx\n", part1, part1);
  printf("part2 %ld / 0x%lx\n", part2, part2);
  printf("part3 %ld / 0x%lx\n", part3, part3);
  printf("part4 %ld / 0x%lx\n", part4, part4);
  return product >> 32;
}

void testMult(uint32_t a_, uint32_t b_) {
  constexpr int half = 16;

  Fixed<uint32_t,32,0> a = a_;
  Fixed<uint32_t,32,0> b = b_;

  Fixed<uint32_t,32,-16> ah = a.dropLSB<16>();
  Fixed<uint32_t,half,0> al = a.dropMSB<16>();
  Fixed<uint32_t,32,-16> bh = b.dropLSB<16>();
  Fixed<uint32_t,half,0> bl = b.dropMSB<16>();

  Fixed<uint32_t,64,-32> ahbh = ah * bh;
  Fixed<uint32_t,32,0> albl = al * bl;
  Fixed<uint32_t,32 + half,half * -1> ahbl = ah * bl;
  Fixed<uint32_t,32 + half,half * -1> albh = al * bh;

  if (true) {
    cout << "a == " << a << endl;
    cout << "b == " << b << endl;
    cout << "ah == " << ah << endl;
    cout << "al == " << al << endl;
    cout << "bh == " << bh << endl;
    cout << "bl == " << bl << endl;

    cout << "ah * bh == " << ahbh << endl;
    cout << "al * bl == " << albl << endl;
    cout << "ah * bl == " << ahbl << endl;
    cout << "al * bh == " << albh << endl;
  }

  Fixed<uint64_t,64,0> part1 = ahbh.expand<uint64_t>().padRight<32>();
  Fixed<uint64_t,64,0> part2 = albl.expand<uint64_t>().padLeft<32>();
  Fixed<uint64_t,64,0> part3 = ahbl.expand<uint64_t>().padRight<16>().padLeft<16>();
  Fixed<uint64_t,64,0> part4 = albh.expand<uint64_t>().padRight<16>().padLeft<16>();
  Fixed<uint64_t,64,0> product = part1 + part2 + part3 + part4;
  cout << product << endl;
  cout << "a * b == " << (uint64_t) product.getIntger() << " vs " << ((uint64_t)(a_ * b_)) << endl;
  cout << endl;
}

uint mulh_orig( int rs1, int rs2 ) {
  return ((int64_t)((int32_t)rs1) * (int64_t)((int32_t)rs2)) >> 32;
}

int testHiMult(int32_t a_, int32_t b_) {
  Fixed<int32_t,32,0> a = a_;
  Fixed<int32_t,32,0> b = b_;

  Fixed<int32_t,32,-16> ah = a.dropLSB<16>();
  Fixed<int32_t,16,0> al = a.dropMSB<16>();
  Fixed<int32_t,32,-16> bh = b.dropLSB<16>();
  Fixed<int32_t,16,0> bl = b.dropMSB<16>();

  auto ahbh = ah * bh;
  auto albl = al * bl;
  auto ahbl = ah * bl;
  auto albh = al * bh;

  Fixed<int64_t,64,0> part1 = ahbh.expand<int64_t>().padRight<32>();
  Fixed<int64_t,64,0> part2 = albl.expand<int64_t>().padLeft<32>();
  Fixed<int64_t,64,0> part3 = ahbl.expand<int64_t>().padRight<16>().padLeft<16>();
  Fixed<int64_t,64,0> part4 = albh.expand<int64_t>().padRight<16>().padLeft<16>();

  Fixed<int64_t,64,0> product = part1 + part2 + part3 + part4;
  //cout << a_ << " * " << b_ << " == " << product << endl;

  uint64_t c = mulh_long(a_, b_);
  uint32_t gcc_result = ((int64_t)a_*(int64_t)b_) >> 32;
  uint c2 = mulh_orig(a_, b_);

  printf("(0x%x * 0x%x) >> 32 -> 0x%16lx (mulh_long)\n", a_, b_, c);
  //printf("(%d * %d) >> 32 -> %ld (mulh_long)\n", a_, b_, c);
  printf("(0x%x * 0x%x) >> 32 -> 0x%16x (gcc)\n", a_, b_, gcc_result);
  //printf("(%d * %d) >> 32 -> %ld (gcc)\n", a_, b_, ((int64_t)a_*(int64_t)b_) >> 32);
  printf("(0x%x * 0x%x) >> 32 -> 0x%16x (mulh_orig)\n", a_, b_, c2);
  //printf("(%d * %d) >> 32 -> %d (mulh_orig)\n", a_, b_, c2);

  testMult(a_, b_);

  if ((c == c2) && (c == gcc_result)) return 1;
  else return 0;
}

int main(int argc, char **argv) {
  if (false) {
    Fixed<uint32_t,30,2> refclk = 50000000;
    Fixed<uint32_t,28,-4> target = atoi(argv[1]) * 16;
    cout << "refclk  " << refclk << endl;
    cout << "target  " << target << endl;
    Fixed<uint32_t,26,6> divisor = refclk / target;
    cout << "divisor " << divisor << endl;
    printf("divisor %" PRIu32 "/0x%" PRIx32 "(%f)\n", divisor.s, divisor.s, (float)divisor.s / 64);
  }

  if (false) {
    Fixed<uint32_t,14,2> a = 4;
    Fixed<uint32_t,14,2> b = 2;
    Fixed<uint32_t,28,4> c = a * b;
    cout << a << " * " << b << " == " << c << endl;
  }

  if (false) {
    Fixed<uint32_t,14,2> a = Fixed<uint32_t,14,2>::fromFloat(3.5);
    Fixed<uint32_t,14,2> b = 2;
    Fixed<uint32_t,28,4> c = a * b;
    cout << a << " * " << b << " == " << c << endl;
  }

  if (false) {
    Fixed<uint32_t,1,20> a = 1;
    Fixed<uint32_t,3,0> b = 3;
    Fixed<uint32_t,1,20> c = a / b;
    cout << a << " / " << b << " == " << c << endl;
  }

  if (false) {
    if (false) {
      Fixed<uint32_t,32,-4> crystal = 54000;
      Fixed<uint32_t,19,10> goal = 108000 * 4;
      Fixed<uint32_t,15,14> divisor = goal / crystal;
      cout << "xtal " << crystal << endl;
      cout << "goal " << goal << endl;
      cout << "divisor " << divisor << endl;
    }

    checkPll(19200000, 500000000);
    checkPll(19200000, 108 * 1000 * 1000 * 5);
    checkPll(54000000, 500000000);
    checkPll(54000000, 108 * 1000 * 1000 * 1);
    checkPll(54000000, 108 * 1000 * 1000 * 2);
    checkPll(54000000, 108 * 1000 * 1000 * 3);
    checkPll(54000000, 108 * 1000 * 1000 * 4);
    checkPll(54000000, 108 * 1000 * 1000 * 5);
  }

  if (false) {
    // the pll-less clock start.S uses
    uint32_t ref = (float)54 * MHz / (0xa050 / 0x1000);
    checkPL011(ref, 9600);
    checkPL011(ref, 115200);
    checkPL011(50 * MHz, 9600);
    checkPL011(50 * MHz, 115200);
    checkPL011(19200000, 115200);
    checkPL011(50 * MHz, 3750 * 1000);
    checkPL011(50 * MHz, 1870 * 1000);
    checkPL011(ref, 300 * 1000);
  }

  if (true) {
    //testMult<uint32_t, 16>(5, 5);
    //testMult<uint32_t>(0x40000, 0x20000);
    //testMult<uint16_t>(0x12345678, 0x12345678);

    int passes = 0;

    for (int i=0; i<10000; i++) {
      int a = rand();
      int b = rand();
      int ret = testHiMult(a, b);
      passes += ret;
      if (!ret) return 1;
    }

    passes += testHiMult(5, 5);
    passes += testHiMult((int32_t)0x17a2e3de, (int32_t)0xa21eb072);
    passes += testHiMult(0x1f652bff, 0xb2bd222b);
    passes += testHiMult(0x72eed63b, 0x7841dbe4);
    passes += testHiMult(0x97d2a670, 0x55b6d312);

    passes += testHiMult(0x0000a000, 0xd000);
    passes += testHiMult(0x90000000, 0xd000);
    passes += testHiMult(0xa670, 0x50000000);

    printf("passed %d times\n", passes);

    //testHiMult(2, -2);
    //testHiMult(0x20000, -0x10000);
    //testHiMult(-0x20000, 0x10000);

    //testHiMult(5, -2);
    //testHiMult(1000, -2);
  }

  if (false) {
    Fixed<uint32_t,15,-4> crystal = 19200;
    Fixed<uint32_t,20,12> goal = 108000 * 5;
    Fixed<uint32_t,16,16> divisor = goal / crystal;
    cout << "xtal " << crystal << endl;
    cout << "goal " << goal << endl;
    cout << "divisor " << divisor << endl;
  }

  if (false) {
    Fixed<uint64_t,16,16> a = 10;
    Fixed<uint8_t,8,0> b = 5;
    Fixed<uint64_t,24,16> c = a * b;
    cout << a << " * " << b << " == " << c << endl;
  }
  return 0;
}
