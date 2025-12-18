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

uint32_t elf_resolve_symbol(elf_context_t* ctx, uint32_t sym_idx) {
	if (!ctx || !ctx->symtab || sym_idx >= ctx->symtab_count) {
		return 0;
	}

	const Elf32_Sym* sym = &ctx->symtab[sym_idx];
	const char* name = ctx->strtab ? (ctx->strtab + sym->st_name) : "";
	uint16_t shndx = sym->st_shndx;

	if (ctx->debug >= 2) {
		printf("[sym] Resolving [%lu] '%s': shndx=%u, value=0x%lx\n", (unsigned long)sym_idx, name, shndx, (unsigned long)sym->st_value);
	}

	if (shndx == SHN_UNDEF) {
		void* addr = elf_lookup_export(name);
		if (addr) {
			if (ctx->debug >= 1) {
				printf("[sym] External '%s' -> %p\n", name, addr);
			}
			return (uint32_t)addr;
		}
		printf("[sym] ERROR: Unresolved '%s'\n", name);
		return 0;
	}

	if (shndx == SHN_ABS) {
		return sym->st_value;
	}

	if (shndx < ctx->section_count && ctx->section_addrs[shndx]) {
		uint32_t addr = (uint32_t)ctx->section_addrs[shndx] + sym->st_value;
		if (ctx->debug >= 2) {
			printf("[sym] '%s' -> sec[%u] + 0x%lx = %p\n", name, shndx, (unsigned long)sym->st_value, (void*)addr);
		}
		return addr;
	}

	printf("[sym] ERROR: Section %u not loaded for '%s'\n", shndx, name);

	return 0;
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
