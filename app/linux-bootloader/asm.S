.global asm_set_ACTLR
asm_set_ACTLR:
  // based on https://github.com/raspberrypi/tools/blob/509f504e8f130ead31b85603016d94d5c854c10c/armstubs/armstub7.S#L78-L82
  mrc p15, 0, r1, c1, c0, 1 @ Read Auxiliary Control Register
  orr r1, r1, r0
  mcr p15, 0, r1, c1, c0, 1 @ Write Auxiliary Control Register
  bx lr
