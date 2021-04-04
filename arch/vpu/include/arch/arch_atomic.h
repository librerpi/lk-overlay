#pragma once
#include <arch/ops.h>

// the first core to read p16 gets a 0, the 2nd core gets a 1
// writing 0 back to it will reset
static inline int atomic_add(volatile int *ptr, int val) {
  int temp;
  bool state;
  uint32_t result;

  state = arch_ints_disabled();
  arch_disable_ints();

  do {
    asm volatile ("mov.m %0, p16": "=r"(result));
  } while (result != 0);

  temp = *ptr;
  *ptr = temp + val;

  // release the hw mutex
  asm volatile("mov.m p16, %0"::"r"(0));

  if (!state) arch_enable_ints();
  return temp;
}
