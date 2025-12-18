#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/uart.h"

#include "uart_receiver.h"

void uart_receiver_init(void) {
	uart_config_t uart_config = {
		.baud_rate = 115200,
		.data_bits = UART_DATA_8_BITS,
		.parity = UART_PARITY_DISABLE,
		.stop_bits = UART_STOP_BITS_1,
		.flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
		.source_clk = UART_SCLK_DEFAULT,
	};
	uart_param_config(UART_NUM_0, &uart_config);
	
	if (uart_is_driver_installed(UART_NUM_0) == false) {
		uart_driver_install(UART_NUM_0, 1024, 0, 0, NULL, 0);
	}
}

uint8_t* uart_receive_data(size_t* out_size) {
	const size_t BUFFER_SIZE = 32768;
	uint8_t* buffer = malloc(BUFFER_SIZE);
	if (!buffer) {
		printf("Error: Failed to allocate buffer.\n");
		return NULL;
	}

	printf("Waiting for binary data...\n");
	fflush(stdout);
	
	vTaskDelay(pdMS_TO_TICKS(100));
	
	uart_flush_input(UART_NUM);
	
	size_t received = 0;
	int timeout = 0;
	const int MAX_TIMEOUT = 50;
	
	while (1) {
		uint8_t byte;
		int len = uart_read_bytes(UART_NUM, &byte, 1, pdMS_TO_TICKS(10));
		
		if (len > 0) {
			timeout = 0;
			buffer[received++] = byte;
			
			if (received >= BUFFER_SIZE) {
				printf("Error: Buffer overflow.\n");
				break;
			}
		} else if (received > 0) {
			timeout++;
			if (timeout > MAX_TIMEOUT) {
				break;
			}
		}
	}
	*out_size = received;
	return buffer;
}

int uart_getchar(void) {
	uint8_t c;
	if (uart_read_bytes(UART_NUM, &c, 1, pdMS_TO_TICKS(10))) {
		return c;
	}
	return -1;
}
