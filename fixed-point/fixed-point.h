#pragma once

#include <climits>
#include <math.h>

template<typename storage, int integer, int fraction> class Fixed {
static_assert(fraction + integer <= sizeof(storage) * CHAR_BIT);
public:
  Fixed(storage value) {
#if 0
    int needed_bits = ceil(log2(value));
    if (needed_bits > integer) {
      printf("warning, Fixed<...,%d,%d> cant hold %ld given to constructor, not enough integer bits, need at least %d\n", integer, fraction, (uint64_t)value, needed_bits);
    }
#endif

    if (fraction == 0) {
      s = value;
    } else if (fraction > 0) {
      s = value << (int)abs(fraction);
    } else {
      s = value >> (int)abs(fraction);
    }
  }

  static Fixed<storage, integer, fraction> fromFloat(float value) {
    if (fraction == 0) return Fixed(value, true);
    else if (fraction > 0) return Fixed(value * (1 << fraction), true);
    else return Fixed(value / (1<<fraction), true);
  }

  Fixed(storage n, bool dummy) {
    s = n;
  }

  storage getIntger() {
    if (fraction == 0) return s;
    else if (fraction > 0) return s >> fraction;
    else return s << (fraction * -1);
  }

  storage getFraction() {
    return s & ( ((storage)1 << fraction) - 1);
  }

  float getFloat() {
    if (fraction == 0) return s;
    else if (fraction > 0) return (float)s / ((storage)1 << abs(fraction));
    else return (float)s * ((storage)1 << abs(fraction));
  }

  template<int a, int b> Fixed<storage,integer + b,fraction - b> operator/ (Fixed<storage,a,b> num) {
    return Fixed<storage,integer + b,fraction - b>(s / num.s, true);
  }

  // TODO, auto-compute storage, based on sum of bits
  template<int a, int b> Fixed<storage,integer + a,fraction + b> operator* (Fixed<storage,a,b> num) {
    return Fixed<storage,integer + a, fraction+b>(s * num.s, true);
  }

  // TODO, return type should be storage or t2, whichever is bigger
  template<typename t2, int a, int b> Fixed<storage,integer + a, fraction + b> operator* (Fixed<t2, a, b> num) {
    static_assert(sizeof(storage) > sizeof(t2));
    return Fixed<storage, integer+a, fraction+b>(s * num.s, true);
  }

  Fixed<storage, integer, fraction> operator+(Fixed<storage, integer, fraction> num) {
    return Fixed<storage, integer, fraction>(s + num.s, true);
  }

  template<int drop> Fixed<storage, integer-drop, fraction> dropMSB() {
    return Fixed<storage, integer-drop, fraction>(s & ((1 << (integer-drop)) - 1), true);
  }

  template<typename outtype> Fixed<outtype, integer, fraction> expand() {
    static_assert(sizeof(outtype) >= sizeof(storage));
    return Fixed<outtype, integer, fraction>(s, true);
  }

  template<int drop> Fixed<storage, integer, fraction-drop> dropLSB() {
    return Fixed<storage, integer, fraction-drop>(s >> drop, true);
  }

  template<int shift> Fixed<storage, integer, fraction + shift> padRight() {
    return Fixed<storage, integer, fraction+shift>(s << shift, true);
  }
  template<int pad> Fixed<storage, pad+integer, fraction> padLeft() {
    return Fixed<storage, pad+integer, fraction>(s, true);
  }

  storage s;
};
