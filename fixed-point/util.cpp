
#ifdef WITH_OSTREAM
#include <iostream>
#endif

#include "fixed-point.h"
#include "util.h"

using namespace std;

Fixed<uint32_t,12,20> computePllDivisorInternal(uint32_t xtal, uint32_t goal) {
  Fixed<uint32_t,32,-4> crystal = xtal;
  Fixed<uint32_t,20,10> goal_freq = goal;
  Fixed<uint32_t,16,14> divisor = goal_freq / crystal;
  Fixed<uint32_t,12,20> shifted_divisor = divisor.dropMSB<4>().padRight<6>();

#ifdef WITH_OSTREAM
  cout << "crystal " << crystal << endl;
  cout << "goal " << goal_freq << endl;
  cout << "divisor " << divisor << endl;
  Fixed<uint32_t,12,14> t = divisor.dropMSB<4>();
  cout << "dropped " << t << endl;
#endif
  return shifted_divisor;
}

extern "C" {
  uint32_t computePllDivisor(uint32_t xtal, uint32_t goal) {
    return computePllDivisorInternal(xtal, goal).s;
  }
}


Fixed<uint32_t,16,6> computePL011Divisor(uint32_t refclk_in, uint32_t baud) {
  Fixed<uint32_t,30,2> refclk = refclk_in;
  Fixed<uint32_t,28,-4> baudclk = baud * 16;
  return (refclk / baudclk).dropMSB<10>();
}
