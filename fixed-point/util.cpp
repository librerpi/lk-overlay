#include "fixed-point.h"
#include "util.h"

Fixed<uint32_t,12,20> computePllDivisor(uint32_t xtal, uint32_t goal) {
  Fixed<uint32_t,32,-4> crystal = xtal;
  Fixed<uint32_t,20,10> goal_freq = goal;
  Fixed<uint32_t,16,14> divisor = goal_freq / crystal;
  Fixed<uint32_t,12,20> shifted_divisor = divisor.dropMSB<4>().padRight<6>();
  return shifted_divisor;
}

Fixed<uint32_t,16,6> computePL011Divisor(uint32_t refclk_in, uint32_t baud) {
  Fixed<uint32_t,30,2> refclk = refclk_in;
  Fixed<uint32_t,28,-4> baudclk = baud * 16;
  return (refclk / baudclk).dropMSB<10>();
}
