#pragma once

#include <platform/bcm28xx.h>

#define ARMCTRL_BASE            (ARM_BASE + 0x000)
#define ARMCTRL_INTC_BASE       (ARM_BASE + 0x200)
#define ARMCTRL_TIMER0_1_BASE   (ARM_BASE + 0x400)
#define ARMCTRL_0_SBM_BASE      (ARM_BASE + 0x800)
#define ARM0_MAILBOX_BASE       (ARM_BASE + 0x880)

#define ARM_CONTROL0  (ARM_BASE+0x000)
#define ARM_C0_SIZ1G      0x00000003
#define ARM_C0_BRESP1     0x00000004
#define ARM_C0_BRESP2     0x00000008
#define ARM_C0_FULLPERI   0x00000040
#define ARM_C0_AARCH64    0x00000200
#define ARM_C0_JTAGGPIO   0x00000C00
#define ARM_C0_APROTPASS  0x0000A000 // Translate 1:1
#define ARM_C0_APROTMSK   0x0000F000
#define ARM_TRANSLATE (ARM_BASE+0x100)
#define ARM_CONTROL1  (ARM_BASE+0x440)
#define ARM_C1_PERSON    0x00000100 // peripherals on
#define ARM_C1_REQSTOP   0x00000200 // ASYNC bridge request stop
#define ARM_ERRHALT   (ARM_BASE + 0x448)
#define ARM_ID        (ARM_BASE + 0x44C)

