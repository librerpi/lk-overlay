#pragma once

typedef void (*hook_init)(void);
typedef void (*hook_msd_probed)(const char *name);

typedef struct {
  const char     *name;
  hook_init       init;
  hook_msd_probed msd_probed;
} usb_hook_t;

void basic_host_init(void);

#define USB_HOOK_START(hookname) const usb_hook_t _hooks_##hookname __USED __ALIGNED(sizeof(void*)) __SECTION("usb_hooks") = { .name = #hookname,
#define USB_HOOK_END };
