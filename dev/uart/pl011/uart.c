/*
 * Copyright (c) 2014-2015 Travis Geiselbrecht
 *
 * Use of this source code is governed by a MIT-style
 * license that can be found in the LICENSE file or at
 * https://opensource.org/licenses/MIT
 */
#include <assert.h>
#include <dev/uart/pl011.h>
#include <kernel/thread.h>
#include <lib/cbuf.h>
#include <lk/reg.h>
#include <lk/trace.h>
#include <platform/clock.h>
#include <platform/debug.h>
#include <platform/interrupts.h>
#include <stdio.h>

#define LOCAL_TRACE 0

/* PL011 implementation */
#define UART_DR    (0x00)
#define UART_RSR   (0x04)
#define UART_TFR   (0x18)
#define UART_ILPR  (0x20)
#define UART_IBRD  (0x24)
#define UART_FBRD  (0x28)
#define UART_LCRH  (0x2c)
#define UART_CR    (0x30)
#define UART_IFLS  (0x34)
#define UART_IMSC  (0x38)
#define UART_TRIS  (0x3c)
#define UART_TMIS  (0x40)
#define UART_ICR   (0x44)
#define UART_DMACR (0x48)

#define UARTREG(base, reg)  (*REG32((base)  + (reg)))

#define RXBUF_SIZE 64

static cbuf_t uart_rx_buf[NUM_UART];
static uintptr_t uart_base[NUM_UART];

static inline uintptr_t uart_to_ptr(unsigned int n) {
    return uart_base[n];
}

static uint32_t calculate_baud_divisor(uint32_t baud) {
  uint32_t uart_freq = get_uart_base_freq();
  if (uart_freq == 0) return 0;
  uint32_t divisor = (uart_freq << 6) / baud / 16;
  return divisor;
}

enum handler_return pl011_uart_irq(void *arg) {
    bool resched = false;
    uint port = (uint)arg;
    uintptr_t base = uart_to_ptr(port);

    /* read interrupt status and mask */
    uint32_t isr = UARTREG(base, UART_TMIS);

    if (isr & ((1<<6) | (1<<4))) { // rtmis, rxmis
        UARTREG(base, UART_ICR) = (1<<4);
        cbuf_t *rxbuf = &uart_rx_buf[port];

        /* while fifo is not empty, read chars out of it */
        while ((UARTREG(base, UART_TFR) & (1<<4)) == 0) {
            /* if we're out of rx buffer, mask the irq instead of handling it */
            if (cbuf_space_avail(rxbuf) == 0) {
              UARTREG(base, UART_IMSC) &= ~(1<<4); // !rxim
              break;
            }

            uint32_t data = UARTREG(base, UART_DR);
            char c = data & 0xff;
            if (data & 0x400) {
              dprintf(INFO, "UART break detected\n");
            } else if (data & 0x100) {
              dprintf(INFO, "UART framing error\n");
            } else {
              if (data & 0x800) {
                dprintf(INFO, "UART input overflow\n");
              }
              cbuf_write_char(rxbuf, c, false);

              resched = true;
            }
// #if CONSOLE_HAS_INPUT_BUFFER
//            if (port == DEBUG_UART) {
//                char c = UARTREG(base, UART_DR);
//                cbuf_write_char(&console_input_cbuf, c, false);
//            } else
//#endif
        }
    }

    return resched ? INT_RESCHEDULE : INT_NO_RESCHEDULE;
}

void pl011_uart_init(int port, int irq) {
    assert(port < NUM_UART);
    uintptr_t base = uart_to_ptr(port);
    // create circular buffer to hold received data
    cbuf_initialize(&uart_rx_buf[port], RXBUF_SIZE);

#ifdef HAVE_REG_IRQ
    register_int_handler(irq, &pl011_uart_irq, (void *)port);
#endif

    UARTREG(base, UART_CR) = 0; // shutdown the entire uart

    // clear all irqs
    UARTREG(base, UART_ICR) = 0x3ff;

    UARTREG(base, UART_LCRH) = (3<<5) // 8bit mode
                | (0<<3) // 2 stop bits
                | (1<<4); // fifo enable

    // set fifo trigger level
    UARTREG(base, UART_IFLS) = 4 << 3; // 7/8 rxfifo, 1/8 txfifo

    // enable rx interrupt
    UARTREG(base, UART_IMSC) = (1<<6)|(1<<4); // rtim, rxim

    // enable receive
    UARTREG(base, UART_CR) |= (1<<9)|(1<<8)|(1<<0); // rxen, tx_enable, uarten

    // enable interrupt
    unmask_interrupt(irq);
}

void pl011_uart_register(int nr, uintptr_t base) {
  assert(nr < NUM_UART);
  uart_base[nr] = base;
}

void pl011_uart_init_early(int port) {
    assert(port < NUM_UART);
    uintptr_t base = uart_to_ptr(port);
    UARTREG(base, UART_CR) = 0; // shutdown the entire uart

    UARTREG(base, UART_LCRH) = (3<<5) // 8bit mode
                | (1<<4); // fifo enable

    // wait for BUSY to clear
    while (UARTREG(base, UART_TFR) & (1<<3));

    // TODO, figure out why this jams
    //UARTREG(base, UART_CR) = (1<<8)|(1<<0); // tx_enable, uarten
}

int pl011_uart_putc(int port, char c) {
  uintptr_t base = uart_to_ptr(port);

  // if enable is clear, dont write
  if ( (UARTREG(base, UART_CR) & 1) == 0) return 1;

  /* spin while fifo is full */
  while (UARTREG(base, UART_TFR) & (1<<5)) ;

  UARTREG(base, UART_DR) = c;

  return 1;
}

int uart_getc(int port, bool wait) {
    cbuf_t *rxbuf = &uart_rx_buf[port];
    LTRACEF("a\n");

    char c;
    if (cbuf_read_char(rxbuf, &c, wait) == 1) {
        UARTREG(uart_to_ptr(port), UART_IMSC) |= (1<<4); // rxim
        return c;
    }

    return -1;
}

/* panic-time getc/putc */
int uart_pputc(int port, char c) {
    uintptr_t base = uart_to_ptr(port);

    /* spin while fifo is full */
    while (UARTREG(base, UART_TFR) & (1<<5))
        ;
    UARTREG(base, UART_DR) = c;

    return 1;
}

int uart_pgetc(int port, bool wait) {
    uintptr_t base = uart_to_ptr(port);

    if ((UARTREG(base, UART_TFR) & (1<<4)) == 0) {
        return UARTREG(base, UART_DR);
    } else {
        return -1;
    }
}


void uart_flush_tx(int port) {
}

void uart_flush_rx(int port) {
}

void pl011_set_baud(int port, uint baud) {
  uintptr_t base = uart_to_ptr(port);
  uint32_t divisor = calculate_baud_divisor(baud);

  UARTREG(base, UART_IBRD) = (divisor >> 6) & 0xffff;
  UARTREG(base, UART_FBRD) = divisor & 0x3f;
}

