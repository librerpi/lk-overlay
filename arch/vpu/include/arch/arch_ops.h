#pragma once

#include <lk/compiler.h>
#include <lk/debug.h>

static inline void arch_enable_ints(void) {
  __asm__ volatile("ei");
}

static inline void arch_disable_ints(void) {
  __asm__ volatile("di");
}

static inline bool arch_ints_disabled(void) {
  uint32_t state;

  __asm__ volatile("mov %0, sr": "=r" (state));
  return !(state & 0x40000000);
}

int vc4_atomic_add(volatile int *ptr, int val);

//static inline int atomic_add(volatile int *ptr, int val) {
//  return vc4_atomic_add(ptr, val);
//}

// todo, use global register instead
// register struct thread *current __asm__("r29");

static inline struct thread *arch_get_current_thread(void) {
  struct thread *thread_reg;
  __asm__ volatile("mov %0, r29" : "=r"(thread_reg));
  return thread_reg;
}

static inline void arch_set_current_thread(struct thread *t) {
  __asm__ volatile ("mov r29, %0" : : "r"(t));
}

static inline ulong arch_cycle_count(void) { return 0; }

static inline uint arch_curr_cpu_num(void) {
  uint32_t cpuid;
  __asm__("version %0" : "=r"(cpuid));
  // TODO, one of the bits in the cpuid is the cpu#, dont remember which one
  // a pdf for an older ARC model says the cpuid contains a 16bit vendor id, 8bit coreid, and 8bit cpuid
  return 0;
}
