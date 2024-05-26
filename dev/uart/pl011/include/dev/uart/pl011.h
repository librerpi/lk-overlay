#pragma once

void pl011_uart_init(int port, int irq);
void pl011_uart_init_early(int nr);
enum handler_return pl011_uart_irq(void *arg);
int uart_getc(int port, bool wait);
int pl011_uart_putc(int port, char c);
void pl011_set_baud(int port, uint baud);
void pl011_uart_register(int nr, uintptr_t base);
