#include <stdio.h>
#include <string.h>

#include <elf.h>
#include "elf_specific.h"

extern uint32_t elf_resolve_symbol(elf_context_t* ctx, uint32_t sym_idx);

int elf_is_iram_section(const Elf32_Shdr* sh, const char* name) {
	if (sh->sh_flags & SHF_EXECINSTR) return 1;
	if (strcmp(name, ".literal") == 0) return 1;
	if (strstr(name, ".literal.")) return 1;
	return 0;
}

int elf_apply_relocations(elf_context_t* ctx) {
	if (!ctx || !ctx->ehdr || !ctx->shdrs) {
		return -1;
	}
	
	if (ctx->debug >= 1) {
		printf("[rel] Processing relocations...\n");
	}
	
	for (uint32_t i = 0; i < ctx->ehdr->e_shnum; i++) {
		const Elf32_Shdr* shdr = &ctx->shdrs[i];
		
		if (shdr->sh_type != SHT_RELA) continue;
		
		const char* name = ctx->shstrtab + shdr->sh_name;
		if (strstr(name, ".xt.") != NULL) continue;

		uint32_t target_idx = shdr->sh_info;
		
		if (ctx->debug >= 1) {
			printf("[rel] Section '%s' -> target [%lu]\n", name, (unsigned long)target_idx);
		}

		uint32_t target_base = ctx->shdrs[target_idx].sh_addr;

		int rela_count = shdr->sh_size / sizeof(Elf32_Rela);

		const Elf32_Rela* relas = (Elf32_Rela*)(ctx->elf_data + shdr->sh_offset);

		for (int r = 0; r < rela_count; r++) {
			const Elf32_Rela* rela = &relas[r];

			int type = rela->r_info & 0xFF;
			int idx = rela->r_info >> 8;

			uint32_t target_address = target_base + rela->r_offset;

			uint32_t symbol_address = elf_resolve_symbol(ctx, idx);

			if (symbol_address == 0 && idx != 0) {
				printf("[rel] Failed to resolve symbol %u\n", idx);
				return -1;
			}

			uint32_t value = symbol_address + rela->r_addend;

			switch (type) {
				case R_XTENSA_32: {
					int is_iram = (target_address >= (uint32_t)ctx->iram_block && target_address < (uint32_t)ctx->iram_block + ctx->iram_size);
					elf_write32((void*)target_address, value, is_iram);
					
					if (ctx->debug >= 2) {
						printf("[rel] R_XTENSA_32: [0x%08lx] = 0x%08lx\n", target_address, value);
					}
					break;
				}
				
				case R_XTENSA_SLOT0_OP: {
					if (ctx->debug >= 2) {
						printf("[rel] R_XTENSA_SLOT0_OP: offset=0x%lx (skipped)\n", rela->r_offset);
					}
					break;
				}
				
				case R_XTENSA_NONE:
					break;
					
				default:
					if (ctx->debug >= 1) {
						printf("[rel] Unknown type %u at offset 0x%lx\n", type, rela->r_offset);
					}
					break;
			}
		}
	}

	return 0;
}

