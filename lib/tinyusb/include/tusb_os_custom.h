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
} osal_queue_def_t;

typedef osal_queue_def_t* osal_queue_t;


#define OSAL_QUEUE_DEF(_int_set, _name, _depth, _type)       \
  uint8_t _name##_buf[_depth*sizeof(_type)];              \
  osal_queue_def_t _name = {                              \
    .ff = TU_FIFO_INIT(_name##_buf, _depth, _type, false) \
  }

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
  //printf("osal_queue_receive(0x%x, 0x%x, 0x%x)\n", (uint32_t)qhdl, (uint32_t)data, msec);
  bool success = tu_fifo_read(&qhdl->ff, data);
  return success;
}

TU_ATTR_ALWAYS_INLINE static inline bool osal_queue_send(osal_queue_t qhdl, void const * data, bool in_isr)
{
  bool success = tu_fifo_write(&qhdl->ff, data);
  TU_ASSERT(success);

  return success;
}

TU_ATTR_ALWAYS_INLINE static inline bool osal_queue_empty(osal_queue_t qhdl)
{
  return tu_fifo_empty(&qhdl->ff);
}
