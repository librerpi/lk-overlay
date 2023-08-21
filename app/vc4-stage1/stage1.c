#include <app.h>
#include <kernel/event.h>
#include <kernel/mutex.h>
#include <kernel/novm.h>
#include <kernel/thread.h>
#include <kernel/wait.h>
#include <lib/elf.h>
#include <lib/fs.h>
#include <lib/io.h>
#include <lk/debug.h>
#include <lk/list.h>
#include <lk/reg.h>
#include <platform.h>
#include <platform/bcm28xx/pll_read.h>
#include <platform/bcm28xx/power.h>
#include <platform/bcm28xx/print_timestamp.h>
#include <platform/bcm28xx/sdram.h>
#include <string.h>
#include <usbhooks.h>

//#include <lua.h>
//#include <lib/lua/lua-utils.h>

#define MB (1024*1024)
#define logf(fmt, ...) { print_timestamp(); printf("[stage1:%s:%d]: "fmt, __FUNCTION__, __LINE__, ##__VA_ARGS__); }

typedef struct {
  struct list_node node;
  const char *name;
} pending_device_t;

static mutex_t pending_device_lock = MUTEX_INITIAL_VALUE(pending_device_lock);
static struct list_node pending_devices = LIST_INITIAL_VALUE(pending_devices);
static event_t pending_devices_nonempty = EVENT_INITIAL_VALUE(pending_devices_nonempty, false, 0);

static ssize_t fs_read_wrapper(struct elf_handle *handle, void *buf, uint64_t offset, size_t len) {
  return fs_read_file(handle->read_hook_arg, buf, offset, len);
}

//static wait_queue_t waiter;

static int waker_entry(void *arg) {
  wait_queue_t *waiter_ = (wait_queue_t*) arg;
  char c;
  int ret = platform_dgetc(&c, true);
  if (ret) {
    puts("failed to getc\n");
    return 0;
  }
  if (c == 'X') {
    printf("got char 0x%x\n", c);
    THREAD_LOCK(state);
    wait_queue_wake_all(waiter_, false, c);
    THREAD_UNLOCK(state);
  }
  return 0;
}

static void *load_and_run_elf(elf_handle_t *stage2_elf) {
  int ret = elf_load(stage2_elf);
  if (ret) {
    printf("failed to load elf: %d\n", ret);
    return NULL;
  }
  elf_close_handle(stage2_elf);
  void *entry = (void*)stage2_elf->entry;
  free(stage2_elf);
  return entry;
}

#if 0
struct xmodem_packet {
  uint8_t magic;
  uint8_t block_num;
  uint8_t block_num_invert;
  uint8_t payload[128];
  uint8_t checksum;
} __attribute__((packed));

//static_assert(sizeof(struct xmodem_packet) == 132, "xmodem packet malformed");

static ssize_t read_repeat(io_handle_t *in, void *buf, ssize_t len) {
  ssize_t total_read = 0;
  ssize_t ret;
  while ((ret = io_read(in, buf, len)) > 0) {
    //printf("0X%02x %d\n\n", ((uint8_t*)buf)[0], ret);
    len -= ret;
    total_read += ret;
    buf += ret;
    if (len <= 0) return total_read;
  }
  return -1;
}

static void xmodem_receive(void) {
  size_t capacity = 2 * MB;
  void *buffer = malloc(capacity);
  struct xmodem_packet *packet = malloc(sizeof(struct xmodem_packet));
  ssize_t ret;
  int blockNr = 1;
  bool success = false;

  io_write(&console_io, "\x15", 1);
  while ((ret = io_read(&console_io, (char*)packet, 1)) == 1) {
    if (packet->magic == 4) {
      puts("R: EOF!");
      success = true;
      break;
    }
    ret = read_repeat(&console_io, &packet->block_num, sizeof(struct xmodem_packet) - 1);
    if (ret != (sizeof(struct xmodem_packet)-1)) {
      puts("read error");
      break;
    }
    uint8_t checksum = 0;
    for (int i=0; i<128; i++) {
      checksum += packet->payload[i];
    }
    bool fail = true;
    if (packet->checksum == checksum) {
      if (packet->block_num_invert == (255 - packet->block_num)) {
        if (packet->block_num == (blockNr & 0xff)) {
          memcpy(buffer + (128 * (blockNr-1)), packet->payload, 128);
          blockNr++;
          io_write(&console_io, "\6", 1);
          fail = false;
        } else if (packet->block_num == ((blockNr - 1) & 0xff)) { // ack was lost, just re-ack
          io_write(&console_io, "\6", 1);
        } else {
          io_write(&console_io, "\x15", 1);
        }
      } else { // block_invert wrong
        io_write(&console_io, "\x15", 1);
      }
    } else { // wrong checksum
      io_write(&console_io, "\x15", 1);
    }
    if (fail) printf("got packet: %d %d %d %d/%d\n", packet->magic, packet->block_num, packet->block_num_invert, packet->checksum, checksum);
  }
  printf("final ret was %ld\n", ret);
  free(packet);
  if (success) {
    elf_handle_t *stage2_elf = malloc(sizeof(elf_handle_t));
    ret = elf_open_handle_memory(stage2_elf, buffer, blockNr*128);
    if (ret) {
      printf("failed to elf open: %ld\n", ret);
      return;
    }
    void *entry = load_and_run_elf(stage2_elf);
    free(buffer);
    arch_chain_load(entry, 0, 0, 0, 0);
    return;
  }
  free(buffer);
}
#endif

static void stage1_init(const struct app_descriptor *app) {
  puts("stage1_init");
}

#if 0
static int chainload(lua_State *L) {
  return 0;
}
#endif

static void try_to_boot(const char *device) {
  int ret;
  logf("trying to boot from %s\n", device);
  ret = fs_mount("/root", "ext2", device);
  if (ret) {
    printf("mount failure: %d\n", ret);
    return;
  }
  filehandle *stage2;
  ret = fs_open_file("/root/boot/lk.elf", &stage2);
  if (ret) {
    printf("failed to open /root/boot/lk.elf: %d\n", ret);
    goto unmount;
  }

  elf_handle_t *stage2_elf = malloc(sizeof(elf_handle_t));
  ret = elf_open_handle(stage2_elf, fs_read_wrapper, stage2, false);
  if (ret) {
    printf("failed to elf open: %d\n", ret);
    goto closefile;
  }
  void *entry = load_and_run_elf(stage2_elf);
  fs_close_file(stage2);
  arch_chain_load(entry, 0, 0, 0, 0);
  return;
  closefile:
    fs_close_file(stage2);
  unmount:
    fs_unmount("/root");
}

static void add_boot_target(const char *device) {
  mutex_acquire(&pending_device_lock);
  logf("considering %s as boot target\n", device);
  pending_device_t *pd = malloc(sizeof(pending_device_t));
  pd->name = device;
  list_add_tail(&pending_devices, &pd->node);
  event_signal(&pending_devices_nonempty, true);
  mutex_release(&pending_device_lock);
}

static void stage1_entry(const struct app_descriptor *app, void *args) {
  int ret;
  puts("stage1 entry\n");
  // sdhost initializes in a blocking mode before threads are ran, so will be available immediately if detected
  // usb initiailizes in a thread and wont show up until stage1_msd_probed() gets called
  add_boot_target("sdhostp1");

  while (true) {
    event_wait(&pending_devices_nonempty);

    mutex_acquire(&pending_device_lock);

    pending_device_t *pd = list_remove_head_type(&pending_devices, pending_device_t, node);
    if (list_is_empty(&pending_devices)) event_unsignal(&pending_devices_nonempty);

    mutex_release(&pending_device_lock);

    try_to_boot(pd->name);
    free(pd);
  }

  /*lua_State *L = lua_newstate(&lua_allocator, NULL);
  register_globals(L);

  luaL_loadstring(L, "print(5+5)");
  ret = lua_pcall(L, 0, LUA_MULTRET, 0);
  printf("lua_pcall == %d\n", ret);
  if (ret == LUA_ERRRUN) {
    lua_prettyprint(L, -1);
  }

  luaL_loadfile(L, "/root/init.lua");

  ret = lua_pcall(L, 0, LUA_MULTRET, 0);
  printf("lua_pcall == %d\n", ret);
  if (ret == LUA_ERRRUN) {
    lua_prettyprint(L, -1);
  }

  lua_close(L); L=NULL;

  return;*/

#if 0
  puts("press X to stop autoboot and go into xmodem mode...");
  wait_queue_init(&waiter);

  thread_t *waker = thread_create("waker", waker_entry, &waiter, DEFAULT_PRIORITY, ARCH_DEFAULT_STACK_SIZE);
  thread_resume(waker);

  THREAD_LOCK(state);
  ret = wait_queue_block(&waiter, 10000);
  THREAD_UNLOCK(state);

  printf("wait result: %d\n", ret);
#else
  ret = 0;
#endif

  if (ret == 'X') {
#if 0
    puts("going into xmodem mode");
    uint32_t rsts = *REG32(PM_RSTS);
    printf("%x\n", rsts);
    xmodem_receive();
#endif
  } else {
  }
}

static void stage1_msd_probed(const char *name) {
  char buffer[64];
  for (int i=0; i<4; i++) {
    snprintf(buffer, 64, "%sp%d", name, i);
    add_boot_target(strdup(buffer));
  }
}

APP_START(stage1)
  .init = stage1_init,
  .entry = stage1_entry,
APP_END

USB_HOOK_START(stage1)
  //.init = bad_apple_usb_init,
  .msd_probed = stage1_msd_probed,
USB_HOOK_END
