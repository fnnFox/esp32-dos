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
		
		if (ctx->debug >= 2) {
			printf("[rel] Section '%s' -> target [%lu]\n", name, target_idx);
		}

		int rela_count = shdr->sh_size / sizeof(Elf32_Rela);

		const Elf32_Rela* relas = (Elf32_Rela*)(ctx->elf_data + shdr->sh_offset);

		for (int r = 0; r < rela_count; r++) {
			const Elf32_Rela* rela = &relas[r];

			int type = rela->r_info & 0xFF;
			int idx = rela->r_info >> 8;

			uint8_t* patch_ptr = (uint8_t*)ctx->elf_data + ctx->shdrs[target_idx].sh_offset + rela->r_offset;
			uint32_t final_address = ctx->shdrs[target_idx].sh_addr + rela->r_offset;

			uint32_t symbol_address = elf_resolve_symbol(ctx, idx);

			if (symbol_address == 0 && idx != 0) {
				printf("[rel] ERROR: Failed to resolve symbol %u\n", idx);
				return -1;
			}

			uint32_t value = symbol_address + rela->r_addend;

			switch (type) {
				case R_XTENSA_32: {
					uint32_t existing = elf_read32((void*)patch_ptr);
					elf_write32((void*)patch_ptr, value + existing);
					
					if (ctx->debug >= 3) {
						printf("[rel] R_XTENSA_32: [0x%08lx] = 0x%08lx\n", final_address, value);
					}
					break;
				}

				case R_XTENSA_SLOT0_OP: {
					uint32_t inst = elf_read24((void*)patch_ptr);
					int op0 = inst & 0x0F;

					if (op0 == 0x01) {  // L32R
						uint32_t pc_aligned = (final_address + 3) & ~3;
						int32_t offset_bytes = (int32_t)(value - pc_aligned);
						int32_t offset_words = offset_bytes >> 2;
						inst = (inst & 0xFF) | ((offset_words & 0xFFFF) << 8);
						elf_write24((void*)patch_ptr, inst);
						
						if (ctx->debug >= 3) {
							printf("[rel] L32R: [0x%08lx] -> 0x%08lx (offset=%ld)\n", 
								   final_address, value, offset_words);
						}
					} else {
						if (ctx->debug >= 2) {
							printf("[rel] SLOT0_OP: [0x%08lx] op0=0x%x (not handled)\n", 
								   final_address, op0);
						}
					}
					break;
				}
				
				case R_XTENSA_NONE:
					break;
					
				default:
					if (ctx->debug >= 2) {
						printf("[rel] Unknown type %u at 0x%08lx\n", type, final_address);
					}
					break;
			}
		}
	}

	if (ctx->debug >= 1) {
		printf("[rel] Relocations done\n");
	}

	return 0;
}

