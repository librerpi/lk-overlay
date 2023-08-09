#include <app.h>
#include <tusb.h>
#include <usbhooks.h>

thread_t *autohost_thread;

static void auto_host_entry(const struct app_descriptor *app, void *args) {
  autohost_thread = get_current_thread();
  basic_host_init();
  puts("doing init");
  tusb_init();
  tuh_init(0);
  puts("init done, spinning on task");
  while (true) {
    //tuh_task();
    tuh_task_ext(UINT32_MAX, false);
  }
}

APP_START(auto_host)
  .entry = auto_host_entry,
APP_END
