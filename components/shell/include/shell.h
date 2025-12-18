#ifndef SHELL_H
#define SHELL_H

#include <stddef.h>

int shell_read_line(char* buffer, size_t size);
int shell_parse_args(char* line, char** argv);

#endif
