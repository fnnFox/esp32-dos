#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <elf.h>
#include "elf_specific.h"
#include "elf_loader.h"

typedef struct {
	const char* name;
	void* address;
} export_entry_t;

void delay(uint32_t ms) {
	vTaskDelay(pdMS_TO_TICKS(ms));
}

static const export_entry_t g_exports[] = {
	// Вывод
	{"printf",		(void*)&printf},
	{"sprintf",		(void*)&sprintf},
	{"snprintf",	(void*)&snprintf},
	{"puts",		(void*)&puts},
	{"putchar",		(void*)&putchar},
	
	// Память
	{"malloc",		(void*)&malloc},
	{"free",		(void*)&free},
	{"calloc",		(void*)&calloc},
	{"realloc",		(void*)&realloc},
	{"memcpy",		(void*)&memcpy},
	{"memset",		(void*)&memset},
	{"memmove",		(void*)&memmove},
	{"memcmp",		(void*)&memcmp},
	
	// Строки
	{"strlen",		(void*)&strlen},
	{"strcmp",		(void*)&strcmp},
	{"strncmp",		(void*)&strncmp},
	{"strcpy",		(void*)&strcpy},
	{"strncpy",		(void*)&strncpy},
	{"strcat",		(void*)&strcat},
	{"strchr",		(void*)&strchr},
	{"strstr",		(void*)&strstr},
	
	// FreeRTOS
	{"delay",		(void*)&delay},

	// Разное
	{"rand",		(void*)&rand},
	{"srand",		(void*)&srand},
	{"abs",			(void*)&abs},
	
	{NULL, NULL}
};

void* elf_lookup_export(const char* name) {
	if (!name || !name[0]) {
		return NULL;
	}

	for (const export_entry_t* e = g_exports; e->name != NULL; e++) {
		if (strcmp(e->name, name) == 0) {
			return e->address;
		}
	}
	return NULL;
}

static void guest_delay_ms(uint32_t ms) {
	vTaskDelay(pdMS_TO_TICKS(ms));
}

guest_api_t guest_api_get_default(void) {
	guest_api_t api = {
		.printf = printf,
		.puts = puts,
		.putchar = putchar,
		.delay_ms = guest_delay_ms,
		.malloc = malloc,
		.free = free,
		.gpio_set_level = NULL,  // TODO: реализовать
		.gpio_get_level = NULL,
	};
	
	memset(api.reserved, 0, sizeof(api.reserved));
	
	return api;
}
