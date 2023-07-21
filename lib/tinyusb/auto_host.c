#include <app.h>
#include <tusb.h>

static void auto_host_entry(const struct app_descriptor *app, void *args) {
  thread_sleep(10000);
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
