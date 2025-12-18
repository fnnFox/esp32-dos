#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "esp_heap_caps.h"
#include "esp32/rom/cache.h"

#include <elf.h>
#include "elf_loader.h"
#include "elf_specific.h"
#include "guest_api.h"

extern void elf_iram_write(void* dst, const void* src, size_t len);
extern int elf_is_iram_section(const Elf32_Shdr* sh, const char* name);
extern int elf_apply_relocations(elf_context_t* ctx);
extern uint32_t elf_resolve_symbol(elf_context_t* ctx, uint32_t sym_idx);

static const char* get_section_name(elf_context_t* ctx, uint32_t idx) {
	if (!ctx->shstrtab || idx >= ctx->ehdr->e_shnum) {
		return "";
	}
	return ctx->shstrtab + ctx->shdrs[idx].sh_name;
}

static size_t align4(size_t x) {
	return (x + 3) & ~3;
}

static int validate_elf(elf_context_t* ctx) {
	if (ctx->elf_size < sizeof(Elf32_Ehdr)) {
		printf("[elf] File too small\n");
		return ELF_ERR_INVALID_FORMAT;
	}
	
	ctx->ehdr = (const Elf32_Ehdr*)ctx->elf_data;
	
	if (memcmp(ctx->ehdr->e_ident, "\x7f""ELF", 4) != 0) {
		printf("[elf] Invalid magic\n");
		return ELF_ERR_INVALID_MAGIC;
	}
	
	if (ctx->ehdr->e_ident[4] != 1) {
		printf("[elf] Not ELF32\n");
		return ELF_ERR_INVALID_FORMAT;
	}
	
	if (ctx->ehdr->e_machine != EM_XTENSA) {
		printf("[elf] Not Xtensa: %d\n", ctx->ehdr->e_machine);
		return ELF_ERR_INVALID_ARCH;
	}
	
	if (ctx->ehdr->e_type != ET_REL) {
		printf("[elf] Not relocatable: type=%d\n", ctx->ehdr->e_type);
		return ELF_ERR_INVALID_FORMAT;
	}
	
	if (ctx->debug >= 1) {
		printf("[elf] Valid ELF: %d sections\n", ctx->ehdr->e_shnum);
	}
	
	return ELF_OK;
}

static int parse_sections(elf_context_t* ctx) {
	ctx->shdrs = (const Elf32_Shdr*)(ctx->elf_data + ctx->ehdr->e_shoff);
	ctx->section_count = ctx->ehdr->e_shnum;
	
	if (ctx->ehdr->e_shstrndx < ctx->section_count) {
		const Elf32_Shdr* shstr = &ctx->shdrs[ctx->ehdr->e_shstrndx];
		ctx->shstrtab = (const char*)(ctx->elf_data + shstr->sh_offset);
	}
	
	for (uint32_t i = 0; i < ctx->section_count; i++) {
		const Elf32_Shdr* sh = &ctx->shdrs[i];
		
		if (sh->sh_type == SHT_SYMTAB) {
			ctx->symtab = (const Elf32_Sym*)(ctx->elf_data + sh->sh_offset);
			ctx->symtab_count = sh->sh_size / sizeof(Elf32_Sym);
			
			if (sh->sh_link < ctx->section_count) {
				const Elf32_Shdr* strtab = &ctx->shdrs[sh->sh_link];
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

static int allocate_memory(elf_context_t* ctx) {
	size_t iram_needed = 0;
	size_t dram_needed = 0;
	
	for (uint32_t i = 0; i < ctx->section_count; i++) {
		const Elf32_Shdr* sh = &ctx->shdrs[i];
		const char* name = get_section_name(ctx, i);
		
		if (!(sh->sh_flags & SHF_ALLOC)) continue;
		if (sh->sh_size == 0) continue;
		
		size_t size = align4(sh->sh_size);
		
		if (elf_is_iram_section(sh, name)) {
			iram_needed += size;
		} else {
			dram_needed += size;
		}
	}
	
	if (ctx->debug >= 1) {
		printf("[elf] Need: IRAM=%d, DRAM=%d\n", iram_needed, dram_needed);
	}
	
	if (iram_needed > 0) {
		ctx->iram_block = heap_caps_malloc(iram_needed, MALLOC_CAP_EXEC | MALLOC_CAP_32BIT);
		if (!ctx->iram_block) {
			printf("[elf] Failed to allocate IRAM\n");
			return ELF_ERR_NO_MEMORY;
		}
		ctx->iram_size = iram_needed;
	}
	
	if (dram_needed > 0) {
		ctx->dram_block = heap_caps_malloc(dram_needed, MALLOC_CAP_8BIT);
		if (!ctx->dram_block) {
			printf("[elf] Failed to allocate DRAM\n");
			if (ctx->iram_block) heap_caps_free(ctx->iram_block);
			return ELF_ERR_NO_MEMORY;
		}
		memset(ctx->dram_block, 0, dram_needed);
		ctx->dram_size = dram_needed;
	}
	
	ctx->section_addrs = calloc(ctx->section_count, sizeof(void*));
	if (!ctx->section_addrs) {
		if (ctx->iram_block) heap_caps_free(ctx->iram_block);
		if (ctx->dram_block) heap_caps_free(ctx->dram_block);
		return ELF_ERR_NO_MEMORY;
	}
	
	return ELF_OK;
}

static int load_sections(elf_context_t* ctx) {
	size_t iram_offset = 0;
	size_t dram_offset = 0;
	
	for (uint32_t i = 0; i < ctx->section_count; i++) {
		const Elf32_Shdr* sh = &ctx->shdrs[i];
		const char* name = get_section_name(ctx, i);
		
		if (!(sh->sh_flags & SHF_ALLOC)) continue;
		if (sh->sh_size == 0) continue;
		
		size_t size = sh->sh_size;
		size_t aligned = align4(size);
		
		if (elf_is_iram_section(sh, name)) {
			void* dest = (uint8_t*)ctx->iram_block + iram_offset;
			elf_iram_write(dest, ctx->elf_data + sh->sh_offset, size);
			ctx->section_addrs[i] = dest;
			iram_offset += aligned;
			
			if (ctx->debug >= 1) {
				printf("[elf] [%2lu] %-20s -> IRAM %p (%lu)\n",
					   (unsigned long)i, name, dest, (unsigned long)size);
			}
		} else {
			void* dest = (uint8_t*)ctx->dram_block + dram_offset;
			
			if (sh->sh_type == SHT_NOBITS) {
				// .bss - уже обнулено
			} else {
				memcpy(dest, ctx->elf_data + sh->sh_offset, size);
			}
			
			ctx->section_addrs[i] = dest;
			dram_offset += aligned;
			
			if (ctx->debug >= 1) {
				printf("[elf] [%2lu] %-20s -> DRAM %p (%lu)\n",
					   (unsigned long)i, name, dest, (unsigned long)size);
			}
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
			uint16_t shndx = sym->st_shndx;
			
			if (shndx != SHN_UNDEF && shndx < ctx->section_count) {
				void* base = ctx->section_addrs[shndx];
				if (base) {
					*out = (guest_entry_t)((uint32_t)base + sym->st_value);
					if (ctx->debug >= 1) {
						printf("[elf] Entry '%s' at %p\n", entry_name, *out);
					}
					return ELF_OK;
				}
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
		.debug_level = 0
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
	
	err = allocate_memory(&ctx);
	if (err != ELF_OK) goto cleanup;
	
	err = load_sections(&ctx);
	if (err != ELF_OK) goto cleanup;
	
	err = elf_apply_relocations(&ctx);
	if (err != 0) {
		err = ELF_ERR_RELOC_FAILED;
		goto cleanup;
	}
	
	Cache_Flush(0);
	
	const char* entry_name = opts ? opts->entry_name : NULL;
	err = find_entry(&ctx, entry_name, &out->entry_point);
	if (err != ELF_OK) goto cleanup;
	
	out->text_mem = ctx.iram_block;
	out->text_size = ctx.iram_size;
	out->data_mem = ctx.dram_block;
	out->data_size = ctx.dram_size;
	
	free(ctx.section_addrs);
	
	return ELF_OK;

cleanup:
	if (ctx.iram_block) heap_caps_free(ctx.iram_block);
	if (ctx.dram_block) heap_caps_free(ctx.dram_block);
	if (ctx.section_addrs) free(ctx.section_addrs);
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
