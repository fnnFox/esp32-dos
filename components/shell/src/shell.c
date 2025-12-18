#include <stdio.h>
#include <string.h>
#include "uart_receiver.h"

#include "shell.h"


int shell_read_line(char* buffer, size_t size) {
	size_t pos = 0;

	while (pos < size - 1) {
		int c = uart_getchar();
		if (c < 0) continue;

		if (c == '\r' || c == '\n') {
			printf("\n");
			buffer[pos] = '\0';
			return pos;
		}

		if (c == 0x7F || c == '\b') {
			if (pos > 0) {
				pos--;
				printf("\b \b");
				fflush(stdout);
			}
			continue;
		}
		
		if (c >= 32 && c < 127) {
			buffer[pos++] = c;
			putchar(c);
			fflush(stdout);
		}
	}
	buffer[pos] = '\0';
	return pos;
}

int shell_parse_args(char* line, char** argv) {
	int argc = 0;
	char* token = strtok(line, " \t");

	while (token) {
		argv[argc++] = token;
		token = strtok(NULL, " \t");
	}
	return argc;
}

