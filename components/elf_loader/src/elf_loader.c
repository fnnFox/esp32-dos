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
	
	// Magic check
	if (memcmp(ctx->ehdr->e_ident, "\x7f""ELF", 4) != 0) {
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
	if (ctx->ehdr->e_type != ET_DYN) {
		printf("[elf] Not DYN file: type=%d\n", ctx->ehdr->e_type);
		return ELF_ERR_INVALID_FORMAT;
	}
	
	// Printing section count
	if (ctx->debug >= 1) {
		printf("[elf] Valid ELF: %d sections\n", ctx->ehdr->e_shnum);
	}
	
	return ELF_OK;
}

static int parse_segments(elf_context_t* ctx) {
	ctx->phdrs = (const Elf32_Phdr*)(ctx->elf_data + ctx->ehdr->e_phoff);
	ctx->phdr_count = ctx->ehdr->e_phnum;

	for (uint32_t i = 0; i < ctx->phdr_count; i++) {
		const Elf32_Phdr* ph = &ctx->phdrs[i];

		switch (ph->p_type) {
			case PT_DYNAMIC:
				ctx->dynamic = (const Elf32_Dyn*)(ctx->elf_data + ph->p_offset);
				break;

			case PT_LOAD:
				if (ph->p_flags & PF_X)	
					ctx->code_phdr = ph;
				else
					ctx->data_phdr = ph;
				break;
		}
	}

	if (!ctx->dynamic || !ctx->code_phdr || !ctx->data_phdr) {
		return ELF_ERR_INVALID_FORMAT;
	}

	for (const Elf32_Dyn* dyn = ctx->dynamic; dyn->d_tag != DT_NULL; dyn++) {
		switch (dyn->d_tag) {
			case DT_HASH:
				// .hash { nbucket, nchain (index=1), etc... }
				ctx->dynsym_count = ((const uint32_t*)(ctx->elf_data + dyn->d_un.d_ptr))[1];
				break;

			case DT_STRTAB:
				ctx->dynstr = (const char*)(ctx->elf_data + dyn->d_un.d_ptr);
				break;

			case DT_SYMTAB:
				ctx->dynsym = (const Elf32_Sym*)(ctx->elf_data + dyn->d_un.d_ptr);
				break;

			case DT_RELA:
				ctx->rela = (const Elf32_Rela*)(ctx->elf_data + dyn->d_un.d_ptr);
				break;

			case DT_RELASZ:
				ctx->rela_count = (dyn->d_un.d_val) / sizeof(Elf32_Rela);
				break;

			case DT_JMPREL:
				ctx->rela_plt = (const Elf32_Rela*)(ctx->elf_data + dyn->d_un.d_ptr);
				break;

			case DT_PLTRELSZ:
				ctx->rela_plt_count = (dyn->d_un.d_val) / sizeof(Elf32_Rela);
				break;
		}
	}

	return ELF_OK;
}

static int allocate_memory(elf_context_t* ctx) {
	ctx->iram_size = ctx->code_phdr->p_memsz;
	ctx->dram_size = ctx->data_phdr->p_memsz;

	if (ctx->debug >= 1) {
		printf("[elf] Need: IRAM=%d, DRAM=%d\n", ctx->iram_size, ctx->dram_size);
	}

	if (ctx->iram_size > 0) {
		ctx->iram_block = heap_caps_malloc(ctx->iram_size, MALLOC_CAP_EXEC | MALLOC_CAP_32BIT);
		if (!ctx->iram_block) goto MEMORY_ERROR;
		memset(ctx->iram_block, 0, ctx->iram_size);
	}
	
	if (ctx->dram_size > 0) {
		ctx->dram_block = heap_caps_malloc(ctx->dram_size, MALLOC_CAP_8BIT);
		if (!ctx->dram_block) goto MEMORY_ERROR;
		memset(ctx->dram_block, 0, ctx->dram_size);
	}

	return ELF_OK;

MEMORY_ERROR:
	if (ctx->iram_block) heap_caps_free(ctx->iram_block);
	if (ctx->dram_block) heap_caps_free(ctx->dram_block);
	return ELF_ERR_NO_MEMORY;
}

static int load_segments(elf_context_t* ctx) {
	elf_iram_write(
			ctx->iram_block,
			ctx->elf_data + ctx->code_phdr->p_offset,
			ctx->code_phdr->p_filesz);

	if (ctx->debug >= 1) {
		printf("[elf] Code: %u bytes -> IRAM %p\n", ctx->code_phdr->p_filesz, ctx->iram_block);
	}
	
	memcpy(
			ctx->dram_block,
			ctx->elf_data + ctx->data_phdr->p_offset,
			ctx->data_phdr->p_filesz);

	if (ctx->debug >= 1) {
		printf("[elf] Data: %u bytes -> DRAM %p\n", ctx->data_phdr->p_filesz, ctx->dram_block);
	}

	ctx->code_bias = (uint32_t)ctx->iram_block - ctx->code_phdr->p_vaddr;

	return ELF_OK;
}

static int find_entry(elf_context_t* ctx, const char* entry_name, guest_entry_t* out) {
	if (!ctx->dynsym || !ctx->dynstr) {
		return ELF_ERR_NO_ENTRY;
	}
	
	if (!entry_name) {
		return ELF_ERR_NO_ENTRY;
	}
	
	for (uint32_t i = 0; i < ctx->dynsym_count; i++) {
		const Elf32_Sym* sym = &ctx->dynsym[i];
		const char* name = ctx->dynstr + sym->st_name;
		
		if (strcmp(name, entry_name) == 0) {
			if  (sym->st_shndx == SHN_UNDEF) {
				break;
			}

			*out = (guest_entry_t)(sym->st_value + ctx->code_bias);

			if (ctx->debug >= 1) {
				printf("[elf] Entry '%s' at %p\n", entry_name, *out);
			}
			return ELF_OK;
		}
	}
	
	printf("[elf] Entry '%s' not found\n", entry_name);
	return ELF_ERR_NO_ENTRY;
}

int elf_load(const uint8_t* elf_data, size_t elf_size, elf_module_t* out) {
	elf_load_options_t opts = {
		.entry_name = NULL,
		.debug_level = 5
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
