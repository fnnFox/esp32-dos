#ifndef UART_RECEIVER_H
#define UART_RECEIVER_H

#include <stddef.h>
#include "driver/uart.h"

#define UART_NUM UART_NUM_0

void uart_receiver_init(void);
uint8_t* uart_receive_data(size_t* out_size);
int uart_getchar();

#endif
