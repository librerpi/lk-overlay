#include <platform/bcm28xx/clock.h>

.global setjmp_post
.global longjmp_pre

.section .text.setjmp
.global setjmp
setjmp:
  mov r1, sr    ; save interrupt state
  di            ; disable interrupts
  st lr, (r0)
  st sp, (r0+4)
  st r6, (r0+8)
  st r7, (r0+12)
  st r8, (r0+16)
  st r9, (r0+20)
  st r10, (r0+24)
  st r11, (r0+28)
  st r12, (r0+32)
  st r13, (r0+36)
  st r14, (r0+40)
  st r15, (r0+44)
  st r16, (r0+48)
  st r17, (r0+52)
  st r18, (r0+56)
  st r19, (r0+60)
  st r20, (r0+64)
  st r21, (r0+68)
  st r22, (r0+72)
  st r23, (r0+76)
  mov sr, r1    ; restore interrupt state
  stm lr, (--sp)
  bl setjmp_post
  mov r0, 0
  ldm pc, (sp++)

.section .text.longjmp
.global longjmp
longjmp:
  mov r6, r0
  mov r7, r1
  mov r2, r3
  bl longjmp_pre
  mov r0, r6
  mov r1, r7

  mov r3, sr    ; save interrupt state
  di            ; disable interrupts
  ld lr, (r0)
  ld sp, (r0+4)
  ld r6, (r0+8)
  ld r7, (r0+12)
  ld r8, (r0+16)
  ld r9, (r0+20)
  ld r10, (r0+24)
  ld r11, (r0+28)
  ld r12, (r0+32)
  ld r13, (r0+36)
  ld r14, (r0+40)
  ld r15, (r0+44)
  ld r16, (r0+48)
  ld r17, (r0+52)
  ld r18, (r0+56)
  ld r19, (r0+60)
  ld r20, (r0+64)
  ld r21, (r0+68)
  ld r22, (r0+72)
  ld r23, (r0+76)
  mov sr, r3    ; restore interrupt state
  mov r0, r1    ; return r1
  rts

.section .text.time
.global time
time:
  mov r1, ST_CLO
  ld r0, (r1)
  rts
