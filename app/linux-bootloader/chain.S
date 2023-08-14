#include <lk/asm.h>

.macro putcv c
  ldr x12, =0xffffffffc0201000
  mov x13, #\c
  str x13, [x12]
.endm

.macro putcp c
  ldr x12, =0x20201000
  mov x13, #\c
  str x13, [x12]
.endm

/* void arm_chain_load(paddr_t entry, ulong arg0, ulong arg1, ulong arg2, ulong arg3) __NO_RETURN; */
/* shut down the system, branching into the secondary system */
FUNCTION(arm_chain_load)
  /* shuffle the args around */
  mov x5, x0
  mov x0, x1
  mov x1, x2
  mov x2, x3
  mov x3, x4

  /* Turn off MMU */
  // LK runs in EL1
  /* Disable MMU */
  mrs x6, sctlr_el1
  bic x6, x6, #(1 << 0) // Disable MMU
  msr sctlr_el1, x6
  isb
  br x5
  b . // should never reach here
END_FUNCTION(arm_chain_load)
