#pragma once

__BEGIN_CDECLS

#include <kernel/event.h>
#include <kernel/spinlock.h>
#include <platform/bcm28xx/arm.h>

void mailbox_init(void);
uint32_t mailbox_fifo_pop(void);

typedef struct mailbox_fifo {
  uint head;
  uint tail;
  uint len_pow2;
  uint32_t *buf;
  event_t event;
  spin_lock_t lock;
} mailbox_fifo_t;

// https://github.com/raspberrypi/firmware/wiki/Mailboxes
// the SoC has 2 mailbox FIFO's, one in each direction
// each one acts as a fifo holding a series of 32bit ints, passed between cpu clusters
// either core cluster can read or write either end of the FIFO's
// but only the intended destination can get an IRQ when data is present
//
// with the current LK codebase, there is a ~1-3 uSec latency between writing to the FIFO, and the irq handler on the other end being started
// the act of writing to the fifo is basically instant (1 MMIO write)
//
// offset + 0x14: sender read, only LS 2 bits
//
// ARM_BASE + 0x900 + 0x80, ARM_1_MAIL0 vpu->arm
// ARM_BASE + 0x900 + 0xA0, ARM_1_MAIL1 arm->vpu
//
// the headers claim there 4 pairs, but all 4 appear to be aliases of same pair
//
// the ARM_1_MAIL pair, are used by the official firmware to form the https://github.com/raspberrypi/firmware/wiki/Mailbox-property-interface
// ARM_1_MAIL1 will trigger VPU IRQ 30 upon receiving data

#define MBOX_ADDR(id2) (ARM_BASE + 0x980 + (0x20 * id2))

#define MAILBOX_DATA(id) (MBOX_ADDR(id))
// the data register (offset + 0) will pop values off the FIFO upon being read
// writing to the data register will push values onto the FIFO

#define MAILBOX_PEEK(id) (MBOX_ADDR(id) + 0x10)
// the peek register (offset + 0x10) will let you read the next value without doing a pop

#define MAILBOX_STATUS(id) (MBOX_ADDR(id) + 0x18)
// offset + 0x18: status read
// MAILBOX status register (...0x98)
#define ARM_MS_FULL       0x80000000
#define ARM_MS_EMPTY      0x40000000
#define ARM_MS_LEVEL      0x400000FF // Max. value depdnds on mailbox depth parameter

#define MAILBOX_CNF(id) (MBOX_ADDR(id) + 0x1c)
// MAILBOX config/status register (offset + 0x1C)
// ANY write to this register clears the error bits!
#define ARM_MC_IHAVEDATAIRQEN     0x00000001 // mailbox irq enable:  has data
#define ARM_MC_IHAVESPACEIRQEN    0x00000002 // mailbox irq enable:  has space
#define ARM_MC_OPPISEMPTYIRQEN    0x00000004 // mailbox irq enable: Opp. is empty
#define ARM_MC_MAIL_CLEAR         0x00000008 // mailbox clear write 1, then  0
#define ARM_MC_IHAVEDATAIRQPEND   0x00000010 // mailbox irq pending:  has space
#define ARM_MC_IHAVESPACEIRQPEND  0x00000020 // mailbox irq pending: Opp. is empty
#define ARM_MC_OPPISEMPTYIRQPEND  0x00000040 // mailbox irq pending
#define ARM_MC_ERRNOOWN           0x00000100 // error : none owner read from mailbox
#define ARM_MC_ERROVERFLW         0x00000200 // error : write to full mailbox
#define ARM_MC_ERRUNDRFLW         0x00000400 // error : read from empty mailbox

__END_CDECLS
