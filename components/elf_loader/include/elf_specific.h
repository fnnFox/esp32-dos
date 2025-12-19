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

	const Elf32_Phdr* phdrs;	// program headers
	uint32_t phdr_count;		// program header count
	
	void* iram_block;			// block of Instriction RAM
	size_t iram_size;			// IRAM size
	void* dram_block;			// block of Data RAM
	size_t dram_size;			// DRAM size

	uint32_t text_vaddr;
	uint32_t data_vaddr;

	const Elf32_Dyn* dynamic;	// .dynamic pointer
	
	const Elf32_Sym* dynsym;	// dynamic symbols
	const char* dynstr;			// dynamic symbol names
	uint32_t dynsym_count;		// dynamic symbol count

	const Elf32_Rela* rela;		// relocation table
	uint32_t rela_count;		// reloc count

	void (*entry)(void);		// entry point
	
	int debug;
} elf_context_t;

void elf_iram_write(void* dst, const void* src, size_t len);
int elf_is_iram_section(const Elf32_Shdr* sh, const char* name);
int elf_apply_relocations(elf_context_t* ctx);
uint32_t elf_resolve_symbol(elf_context_t* ctx, uint32_t sym_idx);
void* elf_lookup_export(const char* name);

#endif
