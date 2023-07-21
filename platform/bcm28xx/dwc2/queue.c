#include <platform/bcm28xx/queue.h>

void queue_init(queue_t *q) {
  spin_lock_init(&q->lock);
  list_initialize(&q->list);
  event_init(&q->event, false, 0);
}

struct list_node *queue_pop(queue_t *q) {
  struct list_node *ret = NULL;
  event_wait(&q->event);
  spin_lock(&q->lock);
  ret = list_remove_head(&q->list);
  if (list_is_empty(&q->list)) event_unsignal(&q->event);
  spin_unlock(&q->lock);
  return ret;
}

void queue_push(queue_t *q, struct list_node *node, bool reschedule) {
  spin_lock(&q->lock);
  list_add_tail(&q->list, node);
  event_signal(&q->event, false);
  spin_unlock(&q->lock);
  if (reschedule) thread_yield();
}
