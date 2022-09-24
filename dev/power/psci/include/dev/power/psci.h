#pragma once

#include <sys/types.h>

extern int psci_call(ulong arg0, ulong arg1, ulong arg2, ulong arg3);
extern uint32_t psci_version(void);
extern int psci_cpu_on(int corenr, ulong entrypoint);
extern void psci_system_off(void);
extern void psci_system_reset(void);

#define PSCI_VERSION  0x84000000
#define SYSTEM_OFF    0x84000008
#define SYSTEM_RESET  0x84000009

#if ARCH_ARM
#define CPU_ON        0x84000003
#endif

#if ARCH_ARM64
#define CPU_ON        0xC4000003
#endif
