#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "esp_heap_caps.h"
#include "esp32/rom/cache.h"
#include "xtensa_context.h"

#include <elf.h>
#include "elf_loader.h"
#include "elf_specific.h"
#include "guest_api.h"

extern int elf_is_iram_section(const Elf32_Shdr* sh, const char* name);
extern int elf_apply_relocations(elf_context_t* ctx);
extern uint32_t elf_resolve_symbol(elf_context_t* ctx, uint32_t sym_idx);

static int validate_elf(elf_context_t* ctx) {
	if (ctx->elf_size < sizeof(Elf32_Ehdr)) {
		printf("[elf] File too small\n");
		return ELF_ERR_INVALID_FORMAT;
	}
	
	ctx->ehdr = (const Elf32_Ehdr*)ctx->elf_data;
	
	// Magic check
	if (memcmp(ctx->ehdr->e_ident, ELFMAG, 4) != 0) {
		printf("[elf] Invalid magic\n");
		return ELF_ERR_INVALID_MAGIC;
	}
	
	// ELF 32-bit check
	if (ctx->ehdr->e_ident[4] != 1) {
		printf("[elf] Not ELF32\n");
		return ELF_ERR_INVALID_FORMAT;
	}
	
	// Architecture check
	if (ctx->ehdr->e_machine != EM_XTENSA) {
		printf("[elf] Not Xtensa: %d\n", ctx->ehdr->e_machine);
		return ELF_ERR_INVALID_ARCH;
	}
	
	// Type check
	if (ctx->ehdr->e_type != ET_REL) {
		printf("[elf] Not relocatable: type=%d\n", ctx->ehdr->e_type);
		return ELF_ERR_INVALID_FORMAT;
	}
	
	// Printing section count
	if (ctx->debug >= 1) {
		printf("[elf] Valid ELF: %d sections\n", ctx->ehdr->e_shnum);
	}
	
	return ELF_OK;
}

static int parse_sections(elf_context_t* ctx) {
	ctx->shdrs = (Elf32_Shdr*)(ctx->elf_data + ctx->ehdr->e_shoff);
	ctx->section_count = ctx->ehdr->e_shnum;
	
	if (ctx->ehdr->e_shstrndx < ctx->section_count) {
		const Elf32_Shdr* shstr = &ctx->shdrs[ctx->ehdr->e_shstrndx];
		ctx->shstrtab = (const char*)(ctx->elf_data + shstr->sh_offset);
	}
	
	for (uint32_t i = 0; i < ctx->section_count; i++) {
		const Elf32_Shdr* shdr = &ctx->shdrs[i];
		
		if (shdr->sh_type == SHT_SYMTAB) {
			ctx->symtab = (const Elf32_Sym*)(ctx->elf_data + shdr->sh_offset);
			ctx->symtab_count = shdr->sh_size / sizeof(Elf32_Sym);
			
			if (shdr->sh_link < ctx->section_count) {
				const Elf32_Shdr* strtab = &ctx->shdrs[shdr->sh_link];
				ctx->strtab = (const char*)(ctx->elf_data + strtab->sh_offset);
			}
			
			if (ctx->debug >= 1) {
				printf("[elf] Found symtab: %lu symbols\n", (unsigned long)ctx->symtab_count);
			}
			break;
		}
	}
	
	return ELF_OK;
}

typedef enum {
	SEC_SKIP,
	SEC_IRAM,
	SEC_DRAM,
	SEC_NULL
} section_load_type_t;

static section_load_type_t get_section_load_type(const Elf32_Shdr* shdr) {
	if (shdr->sh_size == 0)				return SEC_SKIP;
	if (!(shdr->sh_flags & SHF_ALLOC))	return SEC_SKIP;
	if (shdr->sh_flags & SHF_EXECINSTR)	return SEC_IRAM;
	if (shdr->sh_type == SHT_NOBITS)	return SEC_NULL;
	return SEC_DRAM;
}

static void assign_virtual_addresses(elf_context_t* ctx) {
	uint32_t iramv = 0;
	uint32_t dramv = 0;

	for (int i = 0; i < ctx->section_count; i++) {
		Elf32_Shdr* shdr = &ctx->shdrs[i];

		switch (get_section_load_type(shdr)) {
			case SEC_SKIP:
				continue;
			case SEC_IRAM: {
				iramv = ALIGNUP(shdr->sh_addralign, iramv);
				shdr->sh_addr = iramv;
				iramv += shdr->sh_size;
				break;
			}
			case SEC_NULL:
			case SEC_DRAM: {
				dramv = ALIGNUP(shdr->sh_addralign, dramv);
				shdr->sh_addr = dramv;
				dramv += shdr->sh_size;
				break;
			}
		}
	}

	ctx->iram_size = iramv;
	ctx->dram_size = dramv;
}

static int allocate_memory(elf_context_t* ctx) {
	
	if (ctx->debug >= 1) {
		printf("[elf] Need: IRAM=%d, DRAM=%d\n", ctx->iram_size, ctx->dram_size);
	}
	
	if (ctx->iram_size > 0) {
		ctx->iram_block = heap_caps_malloc(ctx->iram_size, MALLOC_CAP_EXEC | MALLOC_CAP_32BIT);
		if (!ctx->iram_block) {
			printf("[elf] Failed to allocate IRAM\n");
			return ELF_ERR_NO_MEMORY;
		}
	}
	
	if (ctx->dram_size > 0) {
		ctx->dram_block = heap_caps_malloc(ctx->dram_size, MALLOC_CAP_8BIT);
		if (!ctx->dram_block) {
			printf("[elf] Failed to allocate DRAM\n");
			if (ctx->iram_block) heap_caps_free(ctx->iram_block);
			return ELF_ERR_NO_MEMORY;
		}
		memset(ctx->dram_block, 0, ctx->dram_size);
	}
	
	return ELF_OK;
}

static void assing_real_addresses(elf_context_t* ctx) {
	for (int i = 0; i < ctx->section_count; i++) {
		Elf32_Shdr* shdr = &ctx->shdrs[i];

		switch (get_section_load_type(shdr)) {
			case SEC_IRAM:
				shdr->sh_addr = (uint32_t)ctx->iram_block + shdr->sh_addr;
				break;
			case SEC_DRAM:
			case SEC_NULL:
				shdr->sh_addr = (uint32_t)ctx->dram_block + shdr->sh_addr;
				break;
			case SEC_SKIP:
				break;
		}
	}
}

static int load_sections(elf_context_t* ctx) {

	for (int i = 0; i < ctx->section_count; i++) {
		Elf32_Shdr* shdr = &ctx->shdrs[i];

		switch (get_section_load_type(shdr)) {
			case SEC_IRAM: {
				const void* src = ctx->elf_data + shdr->sh_offset;
				elf_iram_memcpy((void*)shdr->sh_addr, src, shdr->sh_size);
				printf("[sec] Loaded IRAM section %s at 0x%08lx\n", ctx->shstrtab + shdr->sh_name, shdr->sh_addr);
				break;
			}
			case SEC_DRAM: {
				const void* src = ctx->elf_data + shdr->sh_offset;
				memcpy((void*)shdr->sh_addr, src, shdr->sh_size);
				printf("[sec] Loaded DRAM section %s at 0x%08lx\n", ctx->shstrtab + shdr->sh_name, shdr->sh_addr);
				break;
			}
			case SEC_NULL: {
				memset(shdr->sh_addr, 0, shdr->sh_size);
				printf("[sec] Loaded NULL section %s at 0x%08lx\n", ctx->shstrtab + shdr->sh_name, shdr->sh_addr);
				break;
			}
			case SEC_SKIP:
				break;
		}
	}

	return ELF_OK;
}

static int find_entry(elf_context_t* ctx, const char* entry_name, guest_entry_t* out) {
	if (!ctx->symtab || !ctx->strtab) {
		return ELF_ERR_NO_ENTRY;
	}
	
	if (!entry_name) {
		entry_name = "guest_main";
	}
	
	for (uint32_t i = 0; i < ctx->symtab_count; i++) {
		const Elf32_Sym* sym = &ctx->symtab[i];
		const char* name = ctx->strtab + sym->st_name;
		
		if (strcmp(name, entry_name) == 0) {
			int shndx = sym->st_shndx;
			
			if (shndx != SHN_UNDEF && shndx < ctx->section_count) {
				const Elf32_Shdr* shdr = &ctx->shdrs[shndx];
				*out = (guest_entry_t)(shdr->sh_addr + sym->st_value);
				if (ctx->debug >= 1) {
					printf("[elf] Entry '%s' at %p\n", entry_name, *out);
				}
				return ELF_OK;
			}
			break;
		}
	}
	
	printf("[elf] Entry '%s' not found\n", entry_name);
	return ELF_ERR_NO_ENTRY;
}

int elf_load(const uint8_t* elf_data, size_t elf_size, elf_module_t* out) {
	elf_load_options_t opts = {
		.entry_name = NULL,
		.debug_level = 10
	};
	return elf_load_ex(elf_data, elf_size, &opts, out);
}

int elf_load_ex(const uint8_t* elf_data, size_t elf_size, const elf_load_options_t* opts, elf_module_t* out) {
	if (!elf_data || !out) {
		return ELF_ERR_INVALID_FORMAT;
	}
	
	memset(out, 0, sizeof(*out));
	
	elf_context_t ctx = {0};
	ctx.elf_data = elf_data;
	ctx.elf_size = elf_size;
	ctx.debug = opts ? opts->debug_level : 1;
	
	int err;
	
	err = validate_elf(&ctx);
	if (err != ELF_OK) goto cleanup;
	
	err = parse_sections(&ctx);
	if (err != ELF_OK) goto cleanup;

	assign_virtual_addresses(&ctx);
	
	err = allocate_memory(&ctx);
	if (err != ELF_OK) goto cleanup;

	assing_real_addresses(&ctx);
	
	err = elf_apply_relocations(&ctx);
	if (err != 0) {
		err = ELF_ERR_RELOC_FAILED;
		goto cleanup;
	}
	
	err = load_sections(&ctx);
	if (err != ELF_OK) goto cleanup;


	Cache_Flush(0);
	
	const char* entry_name = opts ? opts->entry_name : NULL;
	err = find_entry(&ctx, entry_name, &out->entry_point);
	if (err != ELF_OK) goto cleanup;
	
	out->text_mem = ctx.iram_block;
	out->text_size = ctx.iram_size;
	out->data_mem = ctx.dram_block;
	out->data_size = ctx.dram_size;
	
	return ELF_OK;

cleanup:
	if (ctx.iram_block) heap_caps_free(ctx.iram_block);
	if (ctx.dram_block) heap_caps_free(ctx.dram_block);
	return err;
}

void elf_unload(elf_module_t* module) {
	if (!module) return;
	
	if (module->text_mem) {
		heap_caps_free(module->text_mem);
	}
	if (module->data_mem) {
		heap_caps_free(module->data_mem);
	}
	
	memset(module, 0, sizeof(*module));
}

const char* elf_strerror(int err) {
	switch (err) {
		case ELF_OK:				return "Success";
		case ELF_ERR_INVALID_MAGIC: return "Invalid ELF magic";
		case ELF_ERR_INVALID_ARCH:	return "Invalid architecture";
		case ELF_ERR_NO_MEMORY:		return "Out of memory";
		case ELF_ERR_NO_ENTRY:		return "Entry point not found";
		case ELF_ERR_RELOC_FAILED:	return "Relocation failed";
		case ELF_ERR_INVALID_FORMAT:return "Invalid format";
		default:					return "Unknown error";
	}
}
