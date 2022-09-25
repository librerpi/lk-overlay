#include <app.h>
#include <platform/bcm28xx/mailbox.h>

struct tagged_packet {
  uint32_t tag;
  uint32_t value_size;
  uint32_t req_resp;
  uint8_t value[0];
};

static void property_init(const struct app_descriptor *app) {
  mailbox_init();
}

/*
 * returns false if the tag has been handled
 */
bool handle_property_tag(struct tagged_packet *packet) {
  uint32_t *value32 = (uint32_t*)(&packet->value[0]);
  switch (packet->tag) {
    case 0x3: // firmware hash
      // TODO, truncate the written reply to fit within packet->value_size
      value32[0] = 0x11223344;
      value32[1] = 0x55667788;
      value32[2] = 0x99aabbcc;
      value32[3] = 0xddeeff00;
      value32[4] = 0x11223344;
      packet->req_resp = 0x80000000 | 20;
      return false;
    case 0x00010005: // get arm mem
      value[0] = 0;
      value[1] = 64 * 1024 * 1024;
      packet->req_resp = 0x80000000 | 8;
      return false;
  }
  return true;
}

void handle_property_message(uint32_t *message) {
  printf("length: %d\n", message[0]);
  printf("full message as uint32's\n");
  for (uint i=0; i < (message[0]/4); i++) {
    printf("%d: 0x%08x\n", i, message[i]);
  }
  printf("req/resp: 0x%08x\n", message[1]);
  bool error = false;
  for (uint position = 2; position < (message[0] / 4);) {
    int start = position;
    struct tagged_packet *packet = (struct tagged_packet *)&message[position];
    position += 12; // the header
    position += packet->value_size;
    position += 4 - (packet->value_size % 4); // padding to align
    printf("offset %d, tag 0x%x, size: %d, req/resp 0x%x\n", start, packet->tag, packet->value_size, packet->req_resp);
    error |= handle_property_tag(packet);
  }
  if (error) message[1] = 0x80000001;
  else message[1] = 0x80000000;
}

static void property_entry(const struct app_descriptor *app, void *args) {
  puts("waiting for property requests");
  while (true) {
    uint32_t msg = mailbox_fifo_pop();
    printf("got prop request 0x%x\n", msg);
    switch (msg & 0xf) {
    case 8: // property tags
      handle_property_message((uint32_t*)(msg & ~0xf));
      break;
    default:
      printf("unsupported mailbox channel %d\n", msg & 0xf);
    }
  }
}

APP_START(properties)
  .init = property_init,
  .entry = property_entry,
APP_END
