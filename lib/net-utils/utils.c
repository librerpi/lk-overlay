#include <assert.h>
#include <kernel/event.h>
#include <lwip/apps/tftp_client.h>
#include <lwip/ip_addr.h>
#include <net-utils.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

typedef struct {
  uint8_t *buffer;
  uint32_t writepos;
  uint32_t totalsize;
  event_t done;
  bool failure;
  const char *path;
} netboot_state_t;

static int netboot_write(void *handle, struct pbuf *p) {
  netboot_state_t *s = (netboot_state_t*)handle;
  assert((p->tot_len + s->writepos) <= s->totalsize);
  assert(p->tot_len == p->len);
  memcpy(s->buffer + s->writepos, p->payload, p->len);
  s->writepos += p->len;
  //printf(".");
  return ERR_OK;
}

static void netboot_close(void *handle) {
  //puts("netboot close");
  netboot_state_t *s = (netboot_state_t*)handle;
  event_signal(&s->done, true);
}

static void netboot_error(void* handle, int err, const char* msg, int size) {
  netboot_state_t *s = (netboot_state_t*)handle;
  printf("tftp error, %d %s %d\n", err, msg, size);
  s->failure = true;
  //event_signal(&s->done, true);
}

static struct tftp_context ctx = {
  .write = netboot_write,
  .close = netboot_close,
  .error = netboot_error,
};

ssize_t tftp_blocking_get(ip_addr_t hostip, const char *path, uint32_t size, uint8_t *buffer) {
  err_t status;

  printf("downloading %s over tftp\n", path);

  netboot_state_t state = {
    .buffer = buffer,
    .writepos = 0,
    .totalsize = size,
    .done = EVENT_INITIAL_VALUE(state.done, false, 0),
    .failure = false,
    .path = path,
  };
    //if (ipaddr_aton(host, &hostip)) {
    //  puts("error parsing IP");
    //  return false;
    //}
  static bool init_done = false;
  if (!init_done) {
    status = tftp_init_client(&ctx);
    if (status != ERR_OK) printf("init status %d\n", status);
    assert(status == ERR_OK);
  }

  thread_sleep(10);

  status = tftp_get(&state, &hostip, 69, path, TFTP_MODE_OCTET);
  if (status != ERR_OK) printf("get status %d\n", status);
  assert(status == ERR_OK);

  event_wait(&state.done);
  puts("");

  if (state.failure) {
    printf("TFTP error %s\n", path);
    return -1;
  }
  return state.writepos;
}
