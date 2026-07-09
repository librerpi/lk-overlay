#pragma once

#include <stdint.h>
#include <stdbool.h>

typedef struct packet_queue_T {
  struct packet_queue_T *next;
  const void *payload;
  int payload_size;
  int start;
  void (*cb)(struct packet_queue_T *);
  bool has_0byte_tail;
} packet_queue_t;

void received_setup_packet(const uint8_t *data);
void dwc2_ep_queue_in(int epNr, const void *buffer, int bytes, void (*cb)(packet_queue_t *));
void dwc2_out_cb(packet_queue_t *pkt);
void dwc2_out_cb_free(packet_queue_t *pkt);
void dwc2_in_cb(packet_queue_t *pkt);
void set_configuration(void);
void usb_reset(void);
void prepare_out(int epNr, int packets, int bytes);
void bulk_out_received(int epNr, const uint8_t *buffer, int size);
