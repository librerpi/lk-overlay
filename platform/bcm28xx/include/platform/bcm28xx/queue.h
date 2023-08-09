#pragma once
#include <kernel/event.h>

typedef struct {
  struct list_node list;
  event_t event;
  spin_lock_t lock;
} queue_t;

void queue_init(queue_t *q);
struct list_node *queue_pop(queue_t *q);
