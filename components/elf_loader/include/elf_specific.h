#ifndef ELF_SPECIFIC_H
#define ELF_SPECIFIC_H

#include <elf.h>
#include <stdint.h>
#include <stddef.h>

#define R_XTENSA_NONE       0
#define R_XTENSA_32         1
#define R_XTENSA_SLOT0_OP   20
#define R_XTENSA_ASM_EXPAND 11

typedef struct {
	const uint8_t* elf_data;	// sources
	size_t elf_size;			// source size
	
	const Elf32_Ehdr* ehdr;		// elf header
	Elf32_Shdr* shdrs;			// section headers
	const char* shstrtab;		// section headers name table
	
	const Elf32_Sym* symtab;	// symbol table
	const char* strtab;			// symbol name table
	uint32_t symtab_count;		// symbol count
	
	uint32_t section_count;		// section count
	
	void* iram_block;			// block of Instriction RAM
	size_t iram_size;			// IRAM size
	void* dram_block;			// block of Data RAM
	size_t dram_size;			// DRAM size
	
	int debug;
} elf_context_t;

void elf_iram_memcpy(void* dst, const void* src, size_t len);
void elf_iram_memset(void* dst, int val, size_t len);
void elf_write32(void* dst, uint32_t value, int is_iram);
uint32_t elf_read32(void* src, int is_iram);

int elf_is_iram_section(const Elf32_Shdr* sh, const char* name);
int elf_apply_relocations(elf_context_t* ctx);
uint32_t elf_resolve_symbol(elf_context_t* ctx, uint32_t sym_idx);
void* elf_lookup_export(const char* name);

#endif
