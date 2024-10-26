#pragma once

#include <lk/list.h>
#include <kernel/event.h>
#include <kernel/mutex.h>

typedef struct {
  event_t evt;
  mutex_t pop_lock;
  spin_lock_t push_lock;
  struct list_node fifo;
} fifo_t;

static void fifo_init(fifo_t *fifo) {
  event_init(&fifo->evt, false, 0);
  mutex_init(&fifo->pop_lock);
  spin_lock_init(&fifo->push_lock);
  list_initialize(&fifo->fifo);
}

struct list_node *fifo_pop(fifo_t *fifo);
void fifo_push(fifo_t *fifo, struct list_node *ln, bool reschedule);
