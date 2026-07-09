#include "usb_types.h"
#include <platform/bcm28xx/clock.h>
#include <string.h>
#include <stdlib.h>
#include <lk/reg.h>
#include <platform/bcm28xx/dwc2.h>
#include "callbacks.h"
#include <stdio.h>

static void get_device_descriptor(int maxLength);
static void get_config_descriptor(int maxLength);
static void get_string_descriptor(const getDescriptorRequest *req);
static void vendor_control_in(void);
static void vendor_control_out(const setupData *s);

static void get_descriptor(const getDescriptorRequest *req) {
  switch (req->bDescriptorType) {
  case 1: // device descriptor
    get_device_descriptor(req->wLength);
    break;
  case 2:
    get_config_descriptor(req->wLength);
    break;
  case 3:
    get_string_descriptor(req);
    break;
  case 5: // endpoint
  case 6: // device qualifier
  case 10: // debug
    dwc2_ep_queue_in(0, NULL, 0, dwc2_out_cb);
    break;
  default:
    printf("unhandled get-descriptor type: %d\n", req->bDescriptorType);
  }
}

void received_setup_packet(const uint8_t *data) {
  setupData *s = (setupData*)data;
  if (s->bmRequestType == 0x80) { // control-in
    puts("control-in transfer");
    switch (s->bRequest) {
    case 0: // get status
      dwc2_ep_queue_in(0, NULL, 0, dwc2_out_cb);
      break;
    case 6: // GET DESCRIPTOR
      puts("GET DESCRIPTOR");
      get_descriptor((getDescriptorRequest*)data);
      break;
    default:
      printf("unhandled control-in type %d\n", s->bRequest);
      break;
    }
  } else if (s->bmRequestType == 0x00) {
    // device->host (OUT), standard, targeting device
    printf("control-out transfer, bRequest %d\n", s->bRequest);
    if (s->bRequest == 0) { // GET_STATUS
      dwc2_ep_queue_in(0, NULL, 0, dwc2_out_cb);
    } else if (s->bRequest == 5) {
      *REG32(USB_DCFG) = ((s->wValue & 0x3f) << 4) | (*REG32(USB_DCFG) & 0xfffff80f);
      dwc2_ep_queue_in(0, NULL, 0, &dwc2_in_cb);
      printf("i am device %d\n", s->wValue);
    } else if (s->bRequest == 9) {
      puts("set configuration");
      set_configuration();
    } else {
      printf("unhandled control-out\n");
    }
  } else if (s->bmRequestType == 0xc0) { // vendor device->host
    // vendor control-in
    vendor_control_in();
  } else if (s->bmRequestType == 0x40) { // vendor host->device
    vendor_control_out(s);
  } else {
    printf("unhandled request type: 0x%x\n", s->bmRequestType);
  }
}

// *************

typedef enum {
  wantingStage2Size = 0,
  wantingStage2,
  wantExit,
} rpiboot_current_state_t;

static rpiboot_current_state_t state;
uint32_t stage2_size;
void *stage2_buffer;
int stage2_offset;
int xfer_pending;

static deviceDescriptor defaultDeviceDescriptor = {
  .bLength = sizeof(deviceDescriptor),
  .bDescriptorType = 1,
//  .bcdUSB = 0x0110, // full speed
  .bcdUSB = 0x0200, // high speed
  .bDeviceClass = 0,
  .bDeviceSubClass = 0,
  .bDeviceProtocol = 0,
  .bMaxPacketSize0 = 64,
  .idVendor = 0x0a5c,
  .idProduct = 0x2764,
  .bcdDevice = 0,
  .iManufacturer = 1,
  .iProduct = 2,
  .iSerialNumber = 4,
  .bNumberConfigurations = 1
};

static const wchar_t *strings[] = {
  [1] = L"manu",
  [2] = L"product",
  [4] = L"serial",
};

static int string_lengths[] = {
  [1] = 8,
  [2] = 14,
  [4] = 12,
};

static struct {
  configurationDescriptor cfg;
  interfaceDescriptor iface0;
  endpointDescriptor ep1out;
  endpointDescriptor ep2in;
} defaultConfigurationDescriptor __attribute__((aligned(4))) = {
  .cfg = {
    .bLength = sizeof(configurationDescriptor),
    .bDescriptorType = 2,
    .wTotalLength = sizeof(defaultConfigurationDescriptor),
    .bNumberInterfaces = 1,
    .bConfigurationValue = 1,
    .iConfiguration = 0,
    .bmAttributes = 0xc0,
    .bMaxPower = 100 / 2,
  },
  .iface0 = {
    .bLength = sizeof(interfaceDescriptor),
    .bDescriptorType = 4,
    .bInterfaceNumber = 0,
    .bAlternateSetting = 0,
    .bNumEndpoints = 2,
    .bInterfaceClass = 0xff,
    .bInterfaceSubClass = 0,
    .bInterfaceProtocol = 0,
    .iInterface = 0
  },
  .ep1out = {
    .bLength = sizeof(endpointDescriptor),
    .bDescriptorType = 5,
    .bEndpointAddress = 1,
    .bmAttributes = 2,
    .wMaxPacketSize = 512,
    .bInterval = 0
  },
  .ep2in = {
    .bLength = sizeof(endpointDescriptor),
    .bDescriptorType = 5,
    .bEndpointAddress = 0x2,
    .bmAttributes = 2,
    .wMaxPacketSize = 512,
    .bInterval = 0
  },
};

#define MAX_PATH_LEN 256
typedef struct {
  uint32_t command; // 0=get size, 1=read file, 2=quit
  char fname[MAX_PATH_LEN];
} file_message_t;

static file_message_t message;

void usb_reset() {
  state = wantingStage2Size;
}

static void get_string_descriptor(const getDescriptorRequest *req) {
  printf("get string descriptor index %d language 0x%x\n", req->bDescriptorIndex, req->wLanguageId);
  switch (req->wLanguageId) {
  case 0: {
      uint8_t *reply = malloc(4);
      reply[0] = 4; // length
      reply[1] = 3; // type string
      reply[2] = 0x09;
      reply[3] = 0x04; // english US
      dwc2_ep_queue_in(0, reply, 4, &dwc2_out_cb_free);
    }
  case 0x0409:{
      int string_index = req->bDescriptorIndex;
      if ((string_index >= 1) && (string_index <= 4)) {
        const int str_len = string_lengths[string_index];

        uint8_t *reply = malloc(str_len + 2);
        reply[0] = str_len + 2;
        reply[1] = 3; // type string
        memcpy(reply+2, strings[string_index], str_len);
        dwc2_ep_queue_in(0, reply, str_len+2, &dwc2_out_cb_free);
      }
      break;
    }
  }
}

static void get_device_descriptor(int maxLength) {
  int size = sizeof(defaultDeviceDescriptor);
  if (size > maxLength) size = maxLength;
  dwc2_ep_queue_in(0, &defaultDeviceDescriptor, size, &dwc2_out_cb);
}

static void get_config_descriptor(int maxLength) {
  int size = sizeof(defaultConfigurationDescriptor);
  if (size > maxLength) size = maxLength;
  dwc2_ep_queue_in(0, &defaultConfigurationDescriptor, size, dwc2_out_cb);
}

static void vendor_control_in() {
  switch (state) {
  case wantingStage2Size:
    message.command = 0;
    strncpy(message.fname, "stage2.elf", MAX_PATH_LEN);
    dwc2_ep_queue_in(0, &message, sizeof(message), dwc2_out_cb);
    break;
  case wantingStage2:
    message.command = 1;
    strncpy(message.fname, "stage2.elf", MAX_PATH_LEN);
    dwc2_ep_queue_in(0, &message, sizeof(message), dwc2_out_cb);
    break;
  case wantExit:
    message.command = 2;
    dwc2_ep_queue_in(0, &message, sizeof(message), dwc2_out_cb);
    break;
  }
}

static void vendor_control_out(const setupData *s) {
  printf("vendor control-out: wIndex 0x%x, wValue 0x%x\n", s->wIndex, s->wValue);
  uint32_t file_size = (s->wIndex << 16) | s->wValue;
  printf("file size: %d\n", file_size);
  if (state == wantingStage2Size) {
    state = wantingStage2;
    stage2_size = file_size;
    stage2_offset = 0;
    stage2_buffer = malloc(file_size);

    int xfer_size = stage2_size;
    int packets = ROUNDUP(xfer_size, 512) / 512;
    if (packets > 1023) {
      printf("clamping from %d to 1023 packets\n", packets);
      packets = 1023;
      xfer_size = packets * 512;
    }
    xfer_pending = xfer_size;
    prepare_out(1, packets, xfer_size);
  }
  dwc2_ep_queue_in(0, NULL, 0, dwc2_out_cb);
}

uint32_t start;

void bulk_out_received(int epNr, const uint8_t *buffer, int size) {
  if (epNr == 1) {
    if (stage2_offset == 0) start = *REG32(ST_CLO);
    //printf("received %d bytes at offset %d\n", size, stage2_offset);
    memcpy(stage2_buffer + stage2_offset, buffer, size);
    stage2_offset += size;
    xfer_pending -= size;

    if (stage2_offset == stage2_size) {
      uint32_t stop = *REG32(ST_CLO);
      puts("xfer complete!");
      state = wantExit;
      printf("%d bytes moved in %d uSec\n", stage2_size, stop - start);
      float duration = stop - start;
      duration = duration / 1000000;
      float speed = (float)stage2_size / duration;
      int speedI = speed;
      printf("%d KB/sec\n", speedI >> 10);

      speed = (float)stage2_size * 8 / duration;
      speedI = speed;
      printf("%d mbit/sec\n", speedI / 1000 / 1000);
    } else if (xfer_pending == 0) {
      int xfer_size = stage2_size - stage2_offset;
      int packets = ROUNDUP(xfer_size, 512) / 512;
      if (packets > 1023) {
        printf("clamping from %d to 1023 packets\n", packets);
        packets = 1023;
        xfer_size = packets * 512;
      }
      xfer_pending = xfer_size;
      prepare_out(1, packets, xfer_size);
    }
  }
}
