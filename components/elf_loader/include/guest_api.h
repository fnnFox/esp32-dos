#ifndef GUEST_API_H
#define GUEST_API_H

#include <stdint.h>
#include <stddef.h>

typedef struct {
	int (*printf)(const char* fmt, ...);
	int (*puts)(const char* s);
	int (*putchar)(int c);

	void (*delay_ms)(uint32_t ms);

	void* (*malloc)(size_t size);
	void (*free)(void* ptr);

	int (*gpio_set_level)(int gpio, int level);
	int (*gpio_get_level)(int gpio);

	void* reserved[8];
} guest_api_t;

typedef int (*guest_entry_t)(guest_api_t* api);

guest_api_t guest_api_get_default(void);

#endif
