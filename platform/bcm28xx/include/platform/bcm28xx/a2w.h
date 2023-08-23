#pragma once

#define A2W_PASSWORD                                             0x5a000000

#define A2W_XOSC_CTRL           (A2W_BASE + 0x190)
#define A2W_SMPS_A_VOLTS        (A2W_BASE + 0x2a0)
#define A2W_XOSC_CTRL_DDREN_SET                            0x00000010
#define A2W_XOSC_CTRL_PLLAEN_SET                           0x00000040
#define A2W_XOSC_CTRL_PLLBEN_SET                           0x00000080
#define A2W_XOSC_CTRL_PLLCEN_SET                           0x00000001
#define A2W_XOSC_CTRL_PLLDEN_SET                           0x00000020

#define A2W_PLLA_DIG0           (A2W_BASE + 0x000)
#define A2W_PLLA_DIG1           (A2W_BASE + 0x004)
#define A2W_PLLA_DIG2           (A2W_BASE + 0x008)
#define A2W_PLLA_DIG3           (A2W_BASE + 0x00c)
#define A2W_PLLA_ANA0           (A2W_BASE + 0x010)
#define A2W_PLLA_ANA1           (A2W_BASE + 0x014)
#define A2W_PLLA_ANA2           (A2W_BASE + 0x018)
#define A2W_PLLA_ANA3           (A2W_BASE + 0x01c)

#define A2W_PLLC_DIG0           (A2W_BASE + 0x020)
#define A2W_PLLC_DIG1           (A2W_BASE + 0x024)
#define A2W_PLLC_DIG2           (A2W_BASE + 0x028)
#define A2W_PLLC_DIG3           (A2W_BASE + 0x02c)
#define A2W_PLLC_ANA0           (A2W_BASE + 0x030)
#define A2W_PLLC_ANA1           (A2W_BASE + 0x034)
#define A2W_PLLC_ANA2           (A2W_BASE + 0x038)
#define A2W_PLLC_ANA3           (A2W_BASE + 0x03c)

#define A2W_PLLD_DIG0           (A2W_BASE + 0x040)
#define A2W_PLLD_ANA0           (A2W_BASE + 0x050)

#define A2W_PLLH_DIG0           (A2W_BASE + 0x060)
#define A2W_PLLH_DIG1           (A2W_BASE + 0x064)
#define A2W_PLLH_DIG2           (A2W_BASE + 0x068)
#define A2W_PLLH_DIG3           (A2W_BASE + 0x06c)
#define A2W_PLLH_ANA0           (A2W_BASE + 0x070)
#define A2W_PLLH_ANA1           (A2W_BASE + 0x074)
#define A2W_PLLH_ANA2           (A2W_BASE + 0x078)
#define A2W_PLLH_ANA3           (A2W_BASE + 0x07c)

#define A2W_PLLB_DIG0           (A2W_BASE + 0x0e0)
#define A2W_PLLB_DIG1           (A2W_BASE + 0x0e4)
#define A2W_PLLB_DIG2           (A2W_BASE + 0x0e8)
#define A2W_PLLB_DIG3           (A2W_BASE + 0x0ec)
#define A2W_PLLB_ANA0           (A2W_BASE + 0x0f0)
#define A2W_PLLB_ANA1           (A2W_BASE + 0x0f4)
#define A2W_PLLB_ANA2           (A2W_BASE + 0x0f8)
#define A2W_PLLB_ANA3           (A2W_BASE + 0x0fc)

#define A2W_PLLB_ANA_MULTI      (A2W_BASE + 0xff0)

#define A2W_PLL_ANA3_KA_LSB     7
#define A2W_PLL_ANA3_KA_MASK    (BIT_MASK(3) << A2W_PLL_ANA3_KA_LSB)
#define A2W_PLL_ANA1_KI_LSB     19
#define A2W_PLL_ANA1_KI_MASK    (BIT_MASK(3) << A2W_PLL_ANA1_KI_LSB)
#define A2W_PLL_ANA1_KP_LSB     15
#define A2W_PLL_ANA1_KP_MASK    (BIT_MASK(4) << A2W_PLL_ANA1_KP_LSB)

// PLLH is special
#define A2W_PLLH_ANA0_KA_LSB       19
#define A2W_PLLH_ANA0_KA_MASK      (BIT_MASK(3) << A2W_PLLH_ANA0_KA_LSB)
#define A2W_PLLH_ANA0_KI_LO_LSB    22
#define A2W_PLLH_ANA1_KI_LO_BITS   2
#define A2W_PLLH_ANA0_KI_LO_MASK   (BIT_MASK(A2W_PLLH_ANA1_KI_LO_BITS) \
                                    << A2W_PLLH_ANA0_KI_LO_LSB)
#define A2W_PLLH_ANA1_KI_HI_LSB    0
#define A2W_PLLH_ANA1_KI_HI_MASK   (BIT_MASK(1) << A2W_PLLH_ANA1_KI_HI_LSB)
#define A2W_PLLH_ANA1_KP_LSB       1
#define A2W_PLLH_ANA1_KP_MASK      (BIT_MASK(4) << A2W_PLLH_ANA1_KP_LSB)



#define A2W_PLLA_FRAC           (A2W_BASE + 0x200)
#define A2W_PLLC_FRAC           (A2W_BASE + 0x220)
#define A2W_PLLD_FRAC           (A2W_BASE + 0x240)
#define A2W_PLLH_FRAC           (A2W_BASE + 0x260)
#define A2W_PLLB_FRAC           (A2W_BASE + 0x2e0)
#define A2W_PLL_FRAC_MASK                                     0x000fffff

// Common A2W_PLL_CTRL bits
#define A2W_PLL_CTRL_PDIV_MASK  0x00007000
#define A2W_PLL_CTRL_PDIV_LSB   12
#define A2W_PLL_CTRL_PWRDN      0x00010000
#define A2W_PLL_CTRL_PRSTN      0x00020000

#define A2W_PLLA_CTRL           (A2W_BASE + 0x100)
#define A2W_PLLA_CTRL_NDIV_SET                             0x000003ff
#define A2W_PLLC_CTRL           (A2W_BASE + 0x120)
#define A2W_PLL_CTRL_PRSTN_SET                             0x00020000
#define A2W_PLLC_CTRL_PWRDN_SET                            0x00010000
#define A2W_PLLC_CTRL_NDIV_SET                             0x000003ff
#define A2W_PLLD_CTRL           (A2W_BASE + 0x140)
#define A2W_PLLD_CTRL_NDIV_SET                             0x000003ff
#define A2W_PLLH_CTRL           (A2W_BASE + 0x160)
#define A2W_PLLH_CTRL_NDIV_SET                             0x000000ff
#define A2W_PLLB_CTRL           (A2W_BASE + 0x1e0)
#define A2W_PLLB_CTRL_NDIV_SET                             0x000003ff

#define A2W_PLLA_DSI0           (A2W_BASE + 0x300)
#define A2W_PLLA_DSI0_CHENB_LSB                            8
#define A2W_PLLA_DSI0_DIV_SET                              0x000000ff
#define A2W_PLLC_CORE2          (A2W_BASE + 0x320)
#define A2W_PLLC_CORE2_CHENB_LSB                           8
#define A2W_PLLC_CORE2_DIV_SET                             0x000000ff
#define A2W_PLLD_DSI0           (A2W_BASE + 0x340)
#define A2W_PLLD_DSI0_CHENB_LSB                            8
#define A2W_PLLD_DSI0_DIV_SET                              0x000000ff
#define A2W_PLLH_AUX            (A2W_BASE + 0x360)
#define A2W_PLLH_AUX_CHENB_LSB                             8
#define A2W_PLLH_AUX_DIV_SET                               0x000000ff
#define A2W_PLLB_ARM            (A2W_BASE + 0x3e0)
#define A2W_PLLB_ARM_CHENB_LSB                             8
#define A2W_PLLB_ARM_DIV_SET                               0x000000ff
#define A2W_PLLA_CORE           (A2W_BASE + 0x400)
#define A2W_PLLA_CORE_CHENB_LSB                            8
#define A2W_PLLA_CORE_DIV_SET                              0x000000ff
#define A2W_PLLD_CORE           (A2W_BASE + 0x440)
#define A2W_PLLD_CORE_CHENB_LSB                            8
#define A2W_PLLD_CORE_DIV_SET                              0x000000ff
#define A2W_PLLH_RCAL           (A2W_BASE + 0x460)
#define A2W_PLLH_RCAL_CHENB_LSB                            8
#define A2W_PLLH_RCAL_DIV_SET                              0x000000ff
#define A2W_PLLB_SP0            (A2W_BASE + 0x4e0)
#define A2W_PLLB_SP0_CHENB_LSB                             8
#define A2W_PLLB_SP0_DIV_SET                               0x000000ff
#define A2W_PLLA_PER            (A2W_BASE + 0x500)
#define A2W_PLLA_PER_CHENB_LSB                             8
#define A2W_PLLA_PER_DIV_SET                               0x000000ff
#define A2W_PLLC_PER            (A2W_BASE + 0x520)
#define A2W_PLLC_PER_CHENB_LSB                             8
#define A2W_PLLC_PER_DIV_SET                               0x000000ff
#define A2W_PLLD_PER            (A2W_BASE + 0x540)
#define A2W_PLLD_PER_CHENB_LSB                             8
#define A2W_PLLD_PER_DIV_SET                               0x000000ff
#define A2W_PLLH_PIX            (A2W_BASE + 0x560)
#define A2W_PLLH_PIX_CHENB_LSB                             8
#define A2W_PLLH_PIX_DIV_SET                               0x000000ff
#define A2W_PLLC_CORE1          (A2W_BASE + 0x420)
#define A2W_PLLC_CORE1_CHENB_LSB                           8
#define A2W_PLLC_CORE1_DIV_SET                             0x000000ff
#define A2W_PLLB_SP1            (A2W_BASE + 0x5e0)
#define A2W_PLLB_SP1_CHENB_LSB                             8
#define A2W_PLLB_SP1_DIV_SET                               0x000000ff
#define A2W_PLLA_CCP2           (A2W_BASE + 0x600)
#define A2W_PLLA_CCP2_CHENB_LSB                            8
#define A2W_PLLA_CCP2_DIV_SET                              0x000000ff
#define A2W_PLLC_CORE0          (A2W_BASE + 0x620)
#define A2W_PLLC_CORE0_CHENB_LSB                           8
#define A2W_PLLC_CORE0_DIV_SET                             0x000000ff
#define A2W_PLLD_DSI1           (A2W_BASE + 0x640)
#define A2W_PLLD_DSI1_CHENB_LSB                            8
#define A2W_PLLD_DSI1_DIV_SET                              0x000000ff
#define A2W_PLLB_SP2            (A2W_BASE + 0x6e0)
#define A2W_PLLB_SP2_CHENB_LSB                             8
#define A2W_PLLB_SP2_DIV_SET                               0x000000ff

// Common A2W_PLL_ANA_KAIP bits
#define A2W_PLL_ANA_KAIP_KA_LSB 8
#define A2W_PLL_ANA_KAIP_KI_LSB 4
#define A2W_PLL_ANA_KAIP_KP_LSB 0

#define A2W_PLLA_ANA_KAIP       (A2W_BASE + 0x310)
#define A2W_PLLC_ANA_KAIP       (A2W_BASE + 0x330)
#define A2W_PLLD_ANA_KAIP       (A2W_BASE + 0x350)
#define A2W_PLLH_ANA_KAIP       (A2W_BASE + 0x370)
#define A2W_PLLB_ANA_KAIP       (A2W_BASE + 0x3f0)

#define A2W_PLLA_ANA_VCO        (A2W_BASE + 0x610)
#define A2W_PLLC_ANA_VCO        (A2W_BASE + 0x630)
#define A2W_PLLD_ANA_VCO        (A2W_BASE + 0x650)
#define A2W_PLLH_ANA_VCO        (A2W_BASE + 0x670)
#define A2W_PLLB_ANA_VCO        (A2W_BASE + 0x6f0)


#define CM_PLLH_ANARST_SET                                 0x00000100
#define A2W_XOSC_CTRL_HDMIEN_SET                           0x00000002
#define CM_PLLH_DIGRST_SET                                 0x00000200
