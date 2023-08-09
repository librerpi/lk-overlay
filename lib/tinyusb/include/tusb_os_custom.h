#pragma once

#include <kernel/mutex.h>
#include <lib/cbuf.h>

typedef mutex_t* osal_mutex_t;
typedef mutex_t osal_mutex_def_t;

#include "common/tusb_fifo.h"

typedef struct
{
    tu_fifo_t ff;
    spin_lock_t spinlock;
    event_t evt;
} osal_queue_def_t;

typedef osal_queue_def_t* osal_queue_t;


#define OSAL_QUEUE_DEF(_int_set, _name, _depth, _type)     \
  uint8_t _name##_buf[_depth*sizeof(_type)];               \
  osal_queue_def_t _name = {                               \
    .ff = TU_FIFO_INIT(_name##_buf, _depth, _type, false), \
    .evt = EVENT_INITIAL_VALUE(_name.evt, false, 0)        \
  }

TU_ATTR_ALWAYS_INLINE static inline bool osal_queue_empty(osal_queue_t qhdl);

TU_ATTR_ALWAYS_INLINE static inline osal_mutex_t osal_mutex_create(osal_mutex_def_t* mdef)
{
  mutex_init(mdef);
  return mdef;
}

TU_ATTR_ALWAYS_INLINE static inline bool osal_mutex_lock (osal_mutex_t mutex_hdl, uint32_t msec)
{
  return mutex_acquire_timeout(mutex_hdl, msec);
}

TU_ATTR_ALWAYS_INLINE static inline bool osal_mutex_unlock(osal_mutex_t mutex_hdl)
{
  mutex_release(mutex_hdl);
  return true;
}

TU_ATTR_ALWAYS_INLINE static inline void osal_task_delay(uint32_t msec) {
  thread_sleep(msec);
}

TU_ATTR_ALWAYS_INLINE static inline osal_queue_t osal_queue_create(osal_queue_def_t* qdef) {
  arch_spin_lock_init(&qdef->spinlock);
  tu_fifo_clear(&qdef->ff);
  return qdef;
}

TU_ATTR_ALWAYS_INLINE static inline bool osal_queue_receive(osal_queue_t qhdl, void* data, uint32_t msec) {
  spin_lock_saved_state_t state;
  //printf("osal_queue_receive(0x%x, 0x%x, 0x%x)\n", (uint32_t)qhdl, (uint32_t)data, msec);
  event_wait_timeout(&qhdl->evt, msec);

  spin_lock_irqsave(&qhdl->spinlock, state);
  bool success = tu_fifo_read(&qhdl->ff, data);
  if (osal_queue_empty(qhdl)) event_unsignal(&qhdl->evt);
  spin_unlock_irqrestore(&qhdl->spinlock, state);
  //printf("osal_queue_receive() == %d\n", success);
  return success;
}

TU_ATTR_ALWAYS_INLINE static inline bool osal_queue_send(osal_queue_t qhdl, void const * data, bool in_isr)
{
  spin_lock_saved_state_t state;
  //printf("osal_queue_send(0x%x, 0x%x, %d)\n", (uint32_t)qhdl, (uint32_t)data, in_isr);
  spin_lock_irqsave(&qhdl->spinlock, state);
  bool success = tu_fifo_write(&qhdl->ff, data);
  TU_ASSERT(success);
  event_signal(&qhdl->evt, false);
  spin_unlock_irqrestore(&qhdl->spinlock, state);

  return success;
}

TU_ATTR_ALWAYS_INLINE static inline bool osal_queue_empty(osal_queue_t qhdl)
{
  return tu_fifo_empty(&qhdl->ff);
}
