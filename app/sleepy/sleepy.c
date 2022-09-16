#include <app.h>
#include <platform/bcm28xx/power.h>
#include <platform.h>
#include <kernel/thread.h>
#include <lk/reg.h>

static void sleepy_entry(const struct app_descriptor *app, void *args) {
  *REG32(PM_RSTS) = 0; // clear reset reason, so it runs partition 0 on reboot
  thread_sleep(2 * 60 * 1000); // 2 minutes
  platform_halt(HALT_ACTION_REBOOT, HALT_REASON_SW_RESET);
}

APP_START(sleepy)
  .entry = sleepy_entry
APP_END
