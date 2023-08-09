#include <host/hcd.h>
#include <lk/console_cmd.h>
#include <lib/hexdump.h>

uint8_t buffer[1024] __attribute__((aligned(32)));

void hcd_event_handler(hcd_event_t const* event, bool in_isr) {
  puts("hcd_event_handler()");
  switch (event->event_id) {
  case HCD_EVENT_DEVICE_ATTACH:
    puts("HCD_EVENT_DEVICE_ATTACH");
    break;
  case HCD_EVENT_DEVICE_REMOVE:
    puts("HCD_EVENT_DEVICE_REMOVE");
    break;
  case HCD_EVENT_XFER_COMPLETE:
    puts("HCD_EVENT_XFER_COMPLETE");
    printf("  ep_addr: %02x\n", event->xfer_complete.ep_addr);
    printf("  result: %d\n", event->xfer_complete.result);
    printf("  len: %d\n", event->xfer_complete.len);
    hexdump_ram(buffer, (uint32_t)buffer, event->xfer_complete.len);
    break;
  case USBH_EVENT_FUNC_CALL:
    puts("USBH_EVENT_FUNC_CALL");
    break;
  }
}

void hcd_devtree_get_info(uint8_t dev_addr, hcd_devtree_info_t* devtree_info) {
  if (devtree_info) {
    devtree_info->rhport = 0;
    devtree_info->hub_addr = 0;
    devtree_info->hub_port = 0;
    devtree_info->speed = TUSB_SPEED_HIGH;
  }
}

static int setup(int argc, const console_cmd_args *argv) {
  uint8_t index = 0;
  uint8_t type = 1;
  uint8_t language_id = 0;
  uint8_t length = 8;
  uint8_t dev_addr = 0;

  tusb_control_request_t const request = {
    .bmRequestType_bit = {
      .recipient = TUSB_REQ_RCPT_DEVICE,
      .type = TUSB_REQ_TYPE_STANDARD,
      .direction = TUSB_DIR_IN
    },
    .bRequest = TUSB_REQ_GET_DESCRIPTOR,
    .wValue = tu_htole16( TU_U16(type, index) ),
    .wIndex = tu_htole16(language_id),
    .wLength = tu_htole16(length)
  };

  hcd_setup_send(0, dev_addr, (uint8_t*)&request);
  return 0;
}

static int reset(int argc, const console_cmd_args *argv) {
  int delay = 50;
  hcd_port_reset(0);
  thread_sleep(delay);
  hcd_port_reset_end(0);
  return 0;
}

static int init(int argc, const console_cmd_args *argv) {
  hcd_init(0);
  hcd_int_enable(0);
  return 0;
}

static int open_ep(int argc, const console_cmd_args *argv) {
  int dev_addr = 0;
  tusb_desc_endpoint_t ep_desc = {
    .bEndpointAddress = 0,
    .bmAttributes = {
      .xfer = 0
    },
    .wMaxPacketSize = 8
  };
  hcd_edpt_open(0, dev_addr, &ep_desc);
  return 0;
}

static int data_in(int argc, const console_cmd_args *argv) {
  int dev_addr = 0;
  int length = 8;
  hcd_edpt_xfer(0, dev_addr, 0x80, buffer, length);
  return 0;
}

STATIC_COMMAND_START
STATIC_COMMAND("init", "hcd init", &init)
STATIC_COMMAND("open", "open an endpoint", &open_ep)
STATIC_COMMAND("reset", "reset the root port", &reset)
STATIC_COMMAND("setup", "send SETUP+DATA", &setup)
STATIC_COMMAND("data_in", "do an IN transfer", &data_in)
STATIC_COMMAND_END(manual_usb);
