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
	const uint8_t* elf_data;
	size_t elf_size;
	
	const Elf32_Ehdr* ehdr;
	const Elf32_Shdr* shdrs;
	const char* shstrtab;
	
	const Elf32_Sym* symtab;
	const char* strtab;
	uint32_t symtab_count;
	
	void** section_addrs;
	uint32_t section_count;
	
	void* iram_block;
	size_t iram_size;
	void* dram_block;
	size_t dram_size;
	
	int debug;
} elf_context_t;

void elf_iram_write(void* dst, const void* src, size_t len);
int elf_is_iram_section(const Elf32_Shdr* sh, const char* name);
int elf_apply_relocations(elf_context_t* ctx);
uint32_t elf_resolve_symbol(elf_context_t* ctx, uint32_t sym_idx);
void* elf_lookup_export(const char* name);

#endif
