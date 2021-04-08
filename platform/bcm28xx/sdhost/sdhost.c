#include <lib/partition.h>
#include <lk/console_cmd.h>
#include <platform/bcm28xx/sdhost_impl.h>

static int cmd_sdhost_init(int argc, const console_cmd_args *argv);

STATIC_COMMAND_START
STATIC_COMMAND("sdhost_init", "initialize the sdhost driver", &cmd_sdhost_init)
STATIC_COMMAND_END(sdhost);

static int cmd_sdhost_init(int argc, const console_cmd_args *argv) {
  rpi_sdhost_init();
  partition_publish("sdhost", 0);
  return 0;
}
