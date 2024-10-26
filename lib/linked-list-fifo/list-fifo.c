#include <assert.h>
#include <linked-list-fifo.h>

struct list_node *fifo_pop(fifo_t *fifo) {
  mutex_acquire(&fifo->pop_lock);
  event_wait(&fifo->evt);
  struct list_node *n = list_remove_tail(&fifo->fifo);
  if (list_is_empty(&fifo->fifo)) event_unsignal(&fifo->evt);
  assert(n);
  mutex_release(&fifo->pop_lock);
  return n;
}

void fifo_push(fifo_t *fifo, struct list_node *ln, bool reschedule) {
  uint32_t state;
  spin_lock_irqsave(&fifo->push_lock, state);
  list_add_head(&fifo->fifo, ln);
  spin_unlock_irqrestore(&fifo->push_lock, state);
  event_signal(&fifo->evt, reschedule);
}
