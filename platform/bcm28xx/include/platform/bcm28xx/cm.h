#pragma once

#define CM_PASSWORD 0x5a000000

#define CM_VPUCTL               (CM_BASE + 0x008)
#define CM_VPUCTL_FRAC_SET                                 0x00000200
#define CM_VPUCTL_BUSY_SET                                 0x00000080
#define CM_VPUCTL_GATE_SET                                 0x00000040
#define CM_VPUDIV               (CM_BASE + 0x00c)
#define CM_PERIICTL             (CM_BASE + 0x020)
#define CM_PERIIDIV             (CM_BASE + 0x024)
#define CM_DPICTL               (CM_BASE + 0x068)
#define CM_DPICTL_KILL_SET 0x20
#define CM_DPICTL_BUSY_SET 0x80
#define CM_DPICTL_ENAB_SET                                 0x00000010
#define CM_DPIDIV               (CM_BASE + 0x06c)
#define CM_TCNTCTL              (CM_BASE + 0x0c0)
#define CM_TCNTCNT              (CM_BASE + 0x0c4)
#define CM_TD0CTL               (CM_BASE + 0x0d0)
#define CM_TD0CTL_ENAB_SET      0x00000010
#define CM_TD0DIV               (CM_BASE + 0x0d4)
#define CM_TIMERCTL             (CM_BASE + 0x0e8)
#define CM_TIMERDIV             (CM_BASE + 0x0ec)
#define CM_UARTCTL              (CM_BASE + 0x0f0)
#define CM_UARTCTL_FRAC_SET                                0x00000200
#define CM_UARTCTL_ENAB_SET                                0x00000010
#define CM_UARTDIV              (CM_BASE + 0x0f4)
#define CM_VECCTL               (CM_BASE + 0x0f8)
#define CM_VECCTL_ENAB_SET                                 0x00000010
#define CM_VECDIV               (CM_BASE + 0x0fc)
#define CM_OSCCOUNT             (CM_BASE + 0x100)
#define CM_PLLA                 (CM_BASE + 0x104)
#define CM_PLLC                 (CM_BASE + 0x108)
#define CM_PLLC_DIGRST_SET                                 0x00000200
#define CM_PLLC_ANARST_SET                                 0x00000100
#define CM_PLLC_HOLDPER_SET                                0x00000080
#define CM_PLLC_HOLDCORE2_SET                              0x00000020
#define CM_PLLC_HOLDCORE1_SET                              0x00000008
#define CM_PLLC_HOLDCORE0_SET                              0x00000002
#define CM_PLLC_LOADCORE0_SET                              0x00000001
#define CM_PLLD                 (CM_BASE + 0x10C)
#define CM_PLLH                 (CM_BASE + 0x110)
#define CM_LOCK                 (CM_BASE + 0x114)
#define CM_LOCK_FLOCKA_BIT      8
#define CM_LOCK_FLOCKB_BIT      9
#define CM_LOCK_FLOCKC_BIT      10
#define CM_LOCK_FLOCKD_BIT      11
#define CM_LOCK_FLOCKH_BIT      12
#define CM_PLLTCTL              (CM_BASE + 0x130)
#define CM_PLLTCTL_KILL_SET     0x00000020
#define CM_PLLTCTL_BUSY_SET     0x00000080
#define CM_PLLTCNT0             (CM_BASE + 0x134)
#define CM_TDCLKEN              (CM_BASE + 0x144)
#define CM_TDCLKEN_PLLDDIV2_SET 0x00000080
#define CM_TDCLKEN_PLLCDIV2_SET 0x00000040
#define CM_TDCLKEN_PLLBDIV2_SET 0x00000020
#define CM_TDCLKEN_PLLADIV2_SET 0x00000010
#define CM_BURSTCTL             (CM_BASE + 0x148)
#define CM_BURSTCTL_ENAB_SET    0x00000010
#define CM_BURSTCNT             (CM_BASE + 0x14c)

#define CM_SRC_OSC                    1
#define CM_SRC_PLLC_CORE0             5


#define CM_PLLB                 (CM_BASE + 0x170)
#define CM_PLLB_LOADARM_SET                               0x00000001
#define CM_PLLB_HOLDARM_SET                               0x00000002
#define CM_PLLB_ANARST_SET                                0x00000100
#define CM_PLLB_DIGRST_SET                                0x00000200

#define CM_ARMCTL               (CM_BASE + 0x1b0)
#define CM_ARMCTL_ENAB_SET                                 0x00000010


// Common CM_PLL bits
#define CM_PLL_ANARST           0x00000100
#define CM_PLL_DIGRST           0x00000200
