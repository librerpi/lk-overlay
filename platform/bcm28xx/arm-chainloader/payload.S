#include <lk/asm.h>

.section .rodata.arm_chainloader, "a"
.balign 4
.global arm_chainloader_start
.global arm_chainloader_end
arm_chainloader_start:
  .incbin ARM_CHAINLOADER_FILE
arm_chainloader_end:
