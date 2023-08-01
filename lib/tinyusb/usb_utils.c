#include <usb_utils.h>

void print_endpoint(const tusb_desc_endpoint_t *desc_ep) {
  printf("      bLength: %d\n", desc_ep->bLength);
  printf("      bDescriptorType: %d\n", desc_ep->bDescriptorType);
  printf("      bEndpointAddress: 0x%x\n", desc_ep->bEndpointAddress);
  printf("      bmAttributes: %d\n", *((uint8_t*)&desc_ep->bmAttributes));
  printf("      wMaxPacketSize: %d\n", desc_ep->wMaxPacketSize);
  printf("      bInterval: %d\n", desc_ep->bInterval);
}
