#include <lk/console_cmd.h>
#include <lk/pow2.h>
#include <lk/reg.h>
#include <platform/bcm28xx/clock.h>
#include <platform/bcm28xx/mailbox.h>
#include <platform/bcm28xx/udelay.h>
#include <platform/interrupts.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

// implements just the dumb FIFO part of the mailbox

static int cmd_mailbox_send(int argc, const console_cmd_args *argv);
static int cmd_mailbox_dump(int argc, const console_cmd_args *argv);
size_t mailbox_fifo_space_avail(void);
void mailbox_fifo_push(uint32_t word);

STATIC_COMMAND_START
STATIC_COMMAND("mailbox_send", "send a word over a mailbox", &cmd_mailbox_send)
STATIC_COMMAND("mailbox_dump", "dump mailbox status and config", &cmd_mailbox_dump)
STATIC_COMMAND_END(mailbox);

void mailbox_send(uint32_t word) {
#ifdef ARCH_VPU
  int id = 0;
#elif defined(ARCH_ARM)
  int id = 1;
#endif
  *REG32(MAILBOX_DATA(id)) = word;
}

static int cmd_mailbox_send(int argc, const console_cmd_args *argv) {
  if (argc < 2) {
    puts("usage: mailbox_send <msg>");
    return 0;
  }

  uint32_t msg = argv[1].u;
  if (msg == 42) msg = *REG32(ST_CLO);
  mailbox_send(msg);
  if (argv[1].u == 42) {
    uint32_t now = *REG32(ST_CLO);
    udelay(5000);
    printf("delta2: %d\n", now - msg);
  }
  return 0;
}

static int cmd_mailbox_dump(int argc, const console_cmd_args *argv) {
  for (int id=0; id < 2; id++) {
    printf("mailbox base: %d 0x%x\n", id, MBOX_ADDR(id));
    printf("   CNF: 0x%x\n", *REG32(MAILBOX_CNF(id)));
    printf("  PEEK: 0x%x\n", *REG32(MAILBOX_PEEK(id)));
    printf("STATUS: 0x%x\n", *REG32(MAILBOX_STATUS(id)));
  }
  return 0;
}

static enum handler_return mailbox_irq(void *arg) {
  uint32_t status = *REG32(MAILBOX_STATUS(1));
  do {
    uint pending = status & 0xff;
    for (uint i=0; i<pending; i++) {
      uint32_t msg = *REG32(MAILBOX_DATA(1));
      mailbox_fifo_push(msg);
    }
    status = *REG32(MAILBOX_STATUS(1));
  } while ((status&ARM_MS_EMPTY) == 0);

  return INT_RESCHEDULE;
}

struct mailbox_fifo fifo;

void mailbox_fifo_push(uint32_t word) {
  spin_lock_saved_state_t state;
  spin_lock_irqsave(&fifo.lock, state);
  if (mailbox_fifo_space_avail() > 0) {
    fifo.buf[fifo.head] = word;
    fifo.head = modpow2(fifo.head + 1, fifo.len_pow2);
    event_signal(&fifo.event, false);
  } else {
    puts("fifo overflow");
  }
  spin_unlock_irqrestore(&fifo.lock, state);
}

uint32_t mailbox_fifo_pop(void) {
  uint32_t result;
retry:
  // work around https://github.com/itszor/gcc-vc4/issues/7
  __asm__ volatile ("nop");
  event_wait(&fifo.event);
  spin_lock_saved_state_t state;
  spin_lock_irqsave(&fifo.lock, state);
  if (fifo.tail != fifo.head) {
    result = fifo.buf[fifo.tail];
    fifo.tail = modpow2(fifo.tail + 1, fifo.len_pow2);
    if (fifo.tail == fifo.head) {
      // we've emptied the buffer, unsignal the event
      event_unsignal(&fifo.event);
    }
  } else {
    spin_unlock_irqrestore(&fifo.lock, state);
    goto retry;
  }
  spin_unlock_irqrestore(&fifo.lock, state);
  return result;
}

size_t mailbox_fifo_space_avail(void) {
    uint consumed = modpow2((uint)(fifo.head - fifo.tail), fifo.len_pow2);
    return valpow2(fifo.len_pow2) - consumed - 1;
}

void mailbox_init() {
#ifdef ARCH_VPU
  int id = 1;
#elif defined(ARCH_ARM)
  int id = 0;
#endif
  printf("mailbox base: %d 0x%x\n", id, MBOX_ADDR(id));

  const uint len = 128;
  fifo.head = 0;
  fifo.tail = 0;
  fifo.len_pow2 = log2_uint(len);
  fifo.buf = malloc(sizeof(uint32_t) * len);
  event_init(&fifo.event, false, 0);
  spin_lock_init(&fifo.lock);

  // enable triggering an irq when data is present
  *REG32(MAILBOX_CNF(id)) = ARM_MC_IHAVEDATAIRQEN;

  register_int_handler(INTERRUPT_ARM, &mailbox_irq, NULL);
  unmask_interrupt(INTERRUPT_ARM);
}
