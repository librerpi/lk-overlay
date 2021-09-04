#pragma once

#include <lk/console_cmd.h>

#define V3D_BASE       0x7ec00000
#define V3D_IDENT0 (V3D_BASE + 0x0000)
#define V3D_IDENT1  (V3D_BASE + 0x0004)
#define V3D_IDENT2  (V3D_BASE + 0x0008)
#define V3D_SCRATCH (V3D_BASE + 0x0010)
#define V3D_L2CACTL (V3D_BASE + 0x0020)
#define V3D_SLCACTL (V3D_BASE + 0x0024)
#define V3D_INTCTL  (V3D_BASE + 0x0030)
#define V3D_INTENA  (V3D_BASE + 0x0034)
#define V3D_INTDIS  (V3D_BASE + 0x0038)

#define V3D_CT0CS  (V3D_BASE + 0x0100)
#define V3D_CT1CS  (V3D_BASE + 0x0104)
#define V3D_CT0EA  (V3D_BASE + 0x0108)
#define V3D_CT1EA  (V3D_BASE + 0x010c)
#define V3D_CT0CA  (V3D_BASE + 0x0110)
#define V3D_CT1CA  (V3D_BASE + 0x0114)

#define V3D_PCS    (V3D_BASE + 0x0130)

#define V3D_RFC    (V3D_BASE + 0x0138)

#define V3D_BXCF   (V3D_BASE + 0x0310)

#define V3D_SQRSV0 (V3D_BASE + 0x0410)
#define V3D_SQRSV1 (V3D_BASE + 0x0414)

#define V3D_PCTRC  (V3D_BASE + 0x0670)
#define V3D_PCTRE  (V3D_BASE + 0x0674)
#define V3D_PCTR0  (V3D_BASE + 0x0680)
#define V3D_PCTRS0 (V3D_BASE + 0x0684)

#define V3D_ERRSTAT (V3D_BASE + 0x0f20)

int cmd_v3d_probe2(int argc, const console_cmd_args *argv);
