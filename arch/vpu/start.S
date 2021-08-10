#include <lk/asm.h>
#include <platform/bcm28xx/clock.h>
#include <platform/bcm28xx/cm.h>
#include <platform/bcm28xx/pl011.h>

#define MANUAL_UART

.section .text.start
FUNCTION(_start)
.global _start
_start:
  di

  // check if the 2nd core ran this entry point
  version r0
  btest r0, 0x10
  bne core2_loop

  mov sp, _fstack
#ifdef MANUAL_UART
  bl manual_uart_cfg
#endif
  bl zero_bss
  bl lk_main
loop:
  b loop

core2_loop:
  b .

.global core2_start
core2_start:
  mov r0, core2_stack_top
  ld sp, (r0)
  //mov r0, 0
  //mov r1, 'A'
  //bl uart_putc
  bl core2_entry
loop2:
  b loop2

.global testit
testit:
  st r1, (r0)
  mov.m r2, p16
  mov.m r3, p16
  mov r4, 0
  mov.m p16, r4
  mov.m r4, p16
  st r2, (r0+4)
  st r3, (r0+8)
  st r4, (r0+12)
  rts

.global toggle18
toggle18:
  mov r0, 0x7e20001c      // set
  mov r1, 0x7e200028      // clear
  mov r2, 1<<18
  mov r3, 100000000
loop18:
  st r2, (r0)             // high
  mov r4, 1
  addcmpbne r4, -1, 0, 0  // delay

  st r2, (r1)             // low
  mov r4, 1
  addcmpbne r4, -1, 0, 0  // delay

  addcmpbne r3,-1,0x0,loop18  // repeat
  rts

#ifdef MANUAL_UART
manual_uart_cfg:
  mov r0, GPIO_GPFSEL1
  mov r1, 0x00004000
  st r1, (r0)
  mov r0, CM_UARTDIV
#if CRYSTAL == 19200000
  mov r1, 0x5a003900 //19.2mhz divisor
#elif CRYSTAL == 54000000
  mov r1, 0x5a00a050 // 54mhz divisor
#else
#error unsupported crystal speed
#endif
  st r1, (r0)
  mov r0, CM_UARTCTL
  mov r1, 0x5a000211
  st r1, (r0)
  mov r0, UART_ICR
  mov r1, 0x000007ff
  st r1, (r0)
  mov r0, UART_LCRH
  mov r1, 0x00000070
  st r1, (r0)
  mov r0, UART_IBRD
  mov r1, 0x00000002
  st r1, (r0)
  mov r0, UART_FBRD
  mov r1, 0x0000003b
  st r1, (r0)
  mov r0, UART_CR
  mov r1, 0x00000301
  st r1, (r0)
  mov r0, UART_DR
  mov r1, 'U'
  st r1, (r0)
  rts
#endif
