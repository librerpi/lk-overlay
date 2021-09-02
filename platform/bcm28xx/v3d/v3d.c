#include <app.h>
#include <lk/console_cmd.h>
#include <lk/reg.h>
#include <platform/bcm28xx/cm.h>
#include <platform/bcm28xx/udelay.h>
#include <stdio.h>

#define ASB_V3D_S_CTRL 0x7e00a008
#define ASB_V3D_M_CTRL 0x7e00a00c
#define CLR_REQ        0x00000001
#define CLR_ACK        0x00000002

#define PM_GRAFX       0x7e10010c

#define CM_V3DCTL      0x7e101038
#define CM_V3DDIV      0x7e10103c

#define V3D_BASE       0x7ec00000
#define V3D_IDENT0 (V3D_BASE + 0x0000)

static int cmd_v3d_probe(int argc, const console_cmd_args *argv);
static int cmd_v3d_probe2(int argc, const console_cmd_args *argv);

STATIC_COMMAND_START
STATIC_COMMAND("v3d_probe", "probe for v3d hw", &cmd_v3d_probe)
STATIC_COMMAND("v3d_probe2", "probe for v3d hw", &cmd_v3d_probe2)
STATIC_COMMAND_END(v3d);

static int cmd_v3d_probe(int argc, const console_cmd_args *argv) {
  printf("ASB_V3D_M_CTRL: 0x%x\n", *REG32(ASB_V3D_M_CTRL));
  printf("ASB_V3D_S_CTRL: 0x%x\n", *REG32(ASB_V3D_S_CTRL));
  printf("PM_GRAFX:       0x%x\n", *REG32(PM_GRAFX));
  printf("CM_V3DCTL:      0x%x\n", *REG32(CM_V3DCTL));
  printf("CM_V3DDIV:      0x%x\n", *REG32(CM_V3DDIV));
  return 0;
}

static int cmd_v3d_probe2(int argc, const console_cmd_args *argv) {
  printf("ident is 0x%x\n", *REG32(V3D_IDENT0));
  return 0;
}

static void v3d_init(const struct app_descriptor *app) {
  *REG32(CM_V3DCTL) = CM_PASSWORD;
  *REG32(CM_V3DDIV) = CM_PASSWORD | (1 << 12);
  *REG32(CM_V3DCTL) = CM_PASSWORD | CM_SRC_PLLC_CORE0;
  *REG32(CM_V3DCTL) = CM_PASSWORD | CM_SRC_PLLC_CORE0 | 0x10;

  *REG32(ASB_V3D_M_CTRL) |= CLR_REQ;
  while ((*REG32(ASB_V3D_M_CTRL) & CLR_ACK) == 0) {}
  *REG32(ASB_V3D_S_CTRL) |= CLR_REQ;
  while ((*REG32(ASB_V3D_S_CTRL) & CLR_ACK) == 0) {}

  udelay(100);
  *REG32(PM_GRAFX) = CM_PASSWORD | (*REG32(PM_GRAFX) & ~0x40); // disable v3d
  udelay(100);
  *REG32(ASB_V3D_S_CTRL) &= ~CLR_REQ;
  while ((*REG32(ASB_V3D_S_CTRL) & CLR_ACK)) {}
  *REG32(ASB_V3D_M_CTRL) &= ~CLR_REQ;
  while ((*REG32(ASB_V3D_M_CTRL) & CLR_ACK)) {}
  udelay(100);

  *REG32(PM_GRAFX) = CM_PASSWORD | (*REG32(PM_GRAFX) | 0x40); // enable v3d
  udelay(1000);
  cmd_v3d_probe(0, 0);
}

APP_START(v3d)
  .init = v3d_init,
APP_END
