#include <stdio.h>
#include <string.h>

#include <elf.h>
#include "elf_specific.h"

extern void* elf_lookup_export(const char* name);
extern uint32_t elf_resolve_symbol(elf_context_t* ctx, uint32_t sym_idx);

void elf_iram_write(void* dst, const void *src, size_t len) {
	volatile uint32_t *d = (volatile uint32_t *)dst;
	const uint8_t *s = (const uint8_t *)src;

	size_t words = len / 4;
	size_t remainder = len % 4;

	for (size_t i = 0; i < words; i++) {
		uint32_t word;
		memcpy(&word, s + (i * 4), 4); 
		d[i] = word;
	}
	if (remainder > 0) {
		uint32_t last_word = 0;
		memcpy(&last_word, s + (words * 4), remainder);
		d[words] = last_word;
	}
}

static void elf_write32(void* dst, uint32_t value, int is_iram) {
	if (is_iram) {
		volatile uint32_t* p = (volatile uint32_t*)dst;
		*p = value;
	} else {
		*(uint32_t*)dst = value;
	}
}

int elf_is_iram(elf_context_t* ctx, uint32_t vaddr) {
	uint32_t start = ctx->code_phdr->p_vaddr;
	uint32_t end = start + ctx->code_phdr->p_memsz;
	printf("[chk] vaddr = 0x%lx, istart = 0x%lx, iend = 0x%lx\n", vaddr, start, end);
	return (vaddr >= start && vaddr < end);
}

static int elf_process_reloc_table(elf_context_t* ctx, const Elf32_Rela* rela, uint32_t count) {
	
	for (uint32_t i = 0; i < count; i++) {
		const Elf32_Rela* rel = &rela[i];
		const uint32_t vaddr = rel->r_offset;
		const int is_iram = elf_is_iram(ctx,vaddr);
		const uint32_t raddr = vaddr + (is_iram ? ctx->code_bias : ctx->data_bias);

		const uint8_t type = rel->r_info & 0xFF;

		uint32_t patch;

		switch (type) {
			case R_XTENSA_NONE:
			case R_XTENSA_RTLD:
			case R_XTENSA_PLT:
				continue;

			case R_XTENSA_32:
			case R_XTENSA_GLOB_DAT:
			case R_XTENSA_JMP_SLOT: {
				const Elf32_Sym* sym = &ctx->dynsym[rel->r_info >> 8];
				if (sym->st_shndx != SHN_UNDEF) {
					uint8_t sym_type = sym->st_info & 0xF;
					uint32_t bias = (sym_type == STT_FUNC) ? ctx->code_bias : ctx->data_bias;
					patch = sym->st_value + bias;
				} else {
					patch = (uint32_t)elf_lookup_export(ctx->dynstr + sym->st_name);
					if (!patch) {
						printf("[rel] Unresolved symbol: %s\n", ctx->dynstr + sym->st_name);
						return -1;
					}
				}
				patch += rel->r_addend;
				break;
			}

			case R_XTENSA_RELATIVE: {
				uint32_t v = *(uint32_t*)raddr;
				uint32_t bias = elf_is_iram(ctx, v) ? ctx->code_bias : ctx->data_bias;
				printf("[rel] bias: 0x%lx\n", bias);
				patch = bias + v;
				printf("[rel] patch: 0x%lx\n", patch);
				break;
			}

			default:
				printf("[rel] Unknown type %d at 0x%lx\n", type, vaddr);
				continue;
		}
		elf_write32((void*)raddr, patch, is_iram);

		if (ctx->debug >= 2) {
			printf("[rel] %ld: type=%d vaddr=0x%lx -> raddr=0x%lx patch=0x%lx %s\n", i, type, vaddr, raddr, patch, is_iram ? "IRAM" : "DRAM");
}
	}
	return 0;
}

int elf_apply_relocations(elf_context_t* ctx) {
	if (!ctx || !ctx->ehdr || !ctx->phdrs) {
		return -1;
	}
	
	int err;

	if (ctx->debug >= 1) {
		printf("[rel] Processing relocations...\n");
	}
	err = elf_process_reloc_table(ctx, ctx->rela, ctx->rela_count);
	if (err) return err;

	if (ctx->debug >= 1) {
		printf("[rel] Processing PLT relocations...\n");
	}
	err = elf_process_reloc_table(ctx, ctx->rela_plt, ctx->rela_plt_count);
	if (err) return err;

	return 0;
}

