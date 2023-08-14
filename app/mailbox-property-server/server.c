#include <app.h>
#include <platform/bcm28xx/mailbox.h>
#include <stdlib.h>
#include <string.h>

struct tagged_packet {
  uint32_t tag;
  uint32_t value_size;
  uint32_t req_resp;
  uint8_t value[0];
};

static void property_init(const struct app_descriptor *app) {
  mailbox_init();
}

static uint32_t hash_chunk(const char *str, int offset) {
  char buffer[9];
  strncpy(buffer, str + (offset*8), 8);
  buffer[8] = 0;
  return strtoll(buffer, NULL, 16);
}

/*
 * returns false if the tag has been handled
 */
bool handle_property_tag(struct tagged_packet *packet) {
  uint32_t *value32 = (uint32_t*)(&packet->value[0]);
  switch (packet->tag) {
    case 0x1: // RPI_FIRMWARE_GET_FIRMWARE_REVISION
      // 32bit unix timestamp of the build, not 2038 safe
      value32[0] = 1691978865;
      packet->req_resp = 0x80000000 | 4;
      return false;
    case 0x2: // RPI_FIRMWARE_GET_FIRMWARE_VARIANT
      // enum for unknown/start/start_x/start_db/start_cd
      value32[0] = 0;
      packet->req_resp = 0x80000000 | 4;
      return false;
    case 0x3: // RPI_FIRMWARE_GET_FIRMWARE_HASH
    {
      struct { uint32_t a; uint32_t b; uint32_t c; uint32_t d; uint32_t e; } hash;
      const char *str = GIT_HASH;
      hash.a = hash_chunk(str, 0);
      hash.b = hash_chunk(str, 1);
      hash.c = hash_chunk(str, 2);
      hash.d = hash_chunk(str, 3);
      hash.e = hash_chunk(str, 4);
      // TODO, truncate the written reply to fit within packet->value_size
      value32[0] = hash.a;
      value32[1] = hash.b;
      value32[2] = hash.c;
      value32[3] = hash.d;
      value32[4] = hash.e;
      packet->req_resp = 0x80000000 | 20;
      return false;
    }
    case 0x00010005: // get arm mem
      value32[0] = 0;
      value32[1] = 64 * 1024 * 1024;
      packet->req_resp = 0x80000000 | 8;
      return false;
    case 0x00010007: // RPI_FIRMWARE_GET_CLOCKS
      return true;
    case 0x00030046: // RPI_FIRMWARE_GET_THROTTLED
      return true;
    case 0x00030048: //RPI_FIRMWARE_NOTIFY_REBOOT
      return true;
    case 0x00030066: // RPI_FIRMWARE_NOTIFY_DISPLAY_DONE
      // used by kms driver, to tell firmware 2d to stop
      return true;
  }
  printf("unsupported tag: 0x%x\n", packet->tag);
  return true;
}

void handle_property_message(uint32_t *message) {
#if 0
  printf("length: %d\n", message[0]);
  printf("full message as uint32's\n");
  for (uint i=0; i < (message[0]/4); i++) {
    printf("%d: 0x%08x\n", i, message[i]);
  }
  printf("req/resp: 0x%08x\n", message[1]);
#endif
  bool error = false;
  for (uint position = 2; position < (message[0] / 4);) {
    int start = position;
    struct tagged_packet *packet = (struct tagged_packet *)&message[position];
    if (packet->tag == 0) break;
    position += 3; // the header
    position += (packet->value_size+3)/4;
    //position += 4 - (packet->value_size % 4); // padding to align
    bool ret = handle_property_tag(packet);
    if (ret) {
      printf("offset %d, tag 0x%x, size: %d, req/resp 0x%x\n", start, packet->tag, packet->value_size, packet->req_resp);
      for (uint i=start; i < position; i++) {
        printf("%d: 0x%08x\n", i, message[i]);
      }
    }
    error |= ret;
  }
  if (error) message[1] = 0x80000001;
  else message[1] = 0x80000000;

  mailbox_send((uint32_t)message | 8);
}

static void property_entry(const struct app_descriptor *app, void *args) {
  puts("waiting for property requests");
  while (true) {
    uint32_t msg = mailbox_fifo_pop();
    //printf("got prop request 0x%x\n", msg);
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
