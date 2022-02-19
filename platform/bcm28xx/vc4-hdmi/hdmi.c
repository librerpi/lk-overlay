#include <platform/bcm28xx/cm.h>

void hdmi_init() {
  power_up_usb();
  setup_pllh(1 * 1000 * 1000 * 1000);

#define CM_HSMCTL (CM_BASE + 0x088)
#define CM_HSMCTL_ENAB_SET                                 0x00000010
#define CM_HSMCTL_KILL_SET                                 0x00000020
#define CM_HSMDIV (CM_BASE + 0x08c)
  int hsm_src = 5;
  *REG32(CM_HSMCTL) = CM_PASSWORD | CM_HSMCTL_KILL_SET | hsm_src;
  *REG32(CM_HSMDIV) = CM_PASSWORD | 0x2000;
  *REG32(CM_HSMCTL) = CM_PASSWORD | CM_HSMCTL_ENAB_SET | hsm_src;
#define HDMI_TX_PHY_TX_PHY_RESET_CTL 0x7e9022c0
  *REG32(HDMI_TX_PHY_TX_PHY_RESET_CTL) = 0xf << 16;
  *REG32(HDMI_TX_PHY_TX_PHY_RESET_CTL) = 0;
#define HDMI_SCHEDULER_CONTROL       0x7e9020c0
  *REG32(HDMI_SCHEDULER_CONTROL) |= HDMI_SCHEDULER_CONTROL_MANUAL_FORMAT_SET | HDMI_SCHEDULER_CONTROL_IGN_VSYNC_PREDS_SET;
}
