#pragma once

#include <platform/bcm28xx.h>

#define UART_DR    (UART0_BASE + 0x00)
#define UART_IBRD  (UART0_BASE + 0x24)
#define UART_FBRD  (UART0_BASE + 0x28)
#define UART_LCRH  (UART0_BASE + 0x2c)
#define UART_CR    (UART0_BASE + 0x30)
#define UART_ICR   (UART0_BASE + 0x44)
