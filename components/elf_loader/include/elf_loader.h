#ifndef ELF_LOADER_H
#define ELF_LOADER_H

#include "guest_api.h"
#include <stddef.h>

typedef enum {
	ELF_OK = 0,
	ELF_ERR_INVALID_MAGIC = -1,
	ELF_ERR_INVALID_ARCH = -2,
	ELF_ERR_NO_MEMORY = -3,
	ELF_ERR_NO_ENTRY = -4,
	ELF_ERR_RELOC_FAILED = -5,
	ELF_ERR_INVALID_FORMAT = -6,
} elf_error_t;

typedef struct {
	void* text_mem;
	void* data_mem;
	size_t text_size;
	size_t data_size;
	guest_entry_t entry_point;
} elf_module_t;

typedef struct {
	const char* entry_name;
	int debug_level;
} elf_load_options_t;

int elf_load(const uint8_t* elf_data, size_t elf_size, elf_module_t* out_module);

int elf_load_ex(const uint8_t* elf_data, size_t elf_size, const elf_load_options_t* options, elf_module_t* out_module);

void elf_unload(elf_module_t* module);

void* elf_find_symbol(elf_module_t* module, const char* name);

const char* str_error(int err);

#endif
