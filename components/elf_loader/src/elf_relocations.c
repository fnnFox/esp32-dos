#include <stdio.h>
#include <string.h>

#include <elf.h>
#include "elf_specific.h"

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

int elf_is_iram_section(const Elf32_Shdr* sh, const char* name) {
	if (sh->sh_flags & SHF_EXECINSTR) return 1;
	if (strcmp(name, ".literal") == 0) return 1;
	if (strstr(name, ".literal.")) return 1;
	return 0;
}

static int apply_r_xtensa_32(elf_context_t* ctx, void* addr, uint32_t sym_value, int32_t addend, int is_iram) {
	uint32_t mem_value;
	if (is_iram) {
		volatile uint32_t* p = (volatile uint32_t*)addr;
		mem_value = *p;
	} else {
		mem_value = *(uint32_t*)addr;
	}
	uint32_t result = sym_value + addend + mem_value;
	
	if (ctx->debug >= 1) {
		printf("[rel] R_XTENSA_32: sym=0x%lx + rela=%ld + mem=0x%lx = 0x%lx\n", (unsigned long)sym_value, (long)addend, (unsigned long)mem_value, (unsigned long)result);
	}
	
	elf_write32(addr, result, is_iram);
	
	return 0;
}

static int apply_r_xtensa_slot0_op(elf_context_t* ctx, void* addr, uint32_t sym_value, int32_t addend) {
	// R_XTENSA_SLOT0_OP используется для модификации инструкций
	// Обычно это L32R или CALL
	// 
	// Для L32R: literal уже пропатчен через R_XTENSA_32
	// Для CALL с -mlongcalls: используется L32R + CALLX, а не прямой CALL
	//
	// Пока просто логируем
	if (ctx->debug >= 1) {
		printf("[rel] R_XTENSA_SLOT0_OP at %p (sym=0x%lx) - skipped\n", 
			   addr, (unsigned long)sym_value);
	}
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
		const Elf32_Shdr* sh = &ctx->shdrs[i];
		
		// Ищем секции RELA
		if (sh->sh_type != SHT_RELA) {
			continue;
		}
		
		const char* name = ctx->shstrtab + sh->sh_name;
		uint32_t target_idx = sh->sh_info;
		
		if (ctx->debug >= 1) {
			printf("[rel] Section '%s' -> target [%lu]\n", name, (unsigned long)target_idx);
		}
		
		// Проверяем, загружена ли целевая секция
		if (target_idx >= ctx->section_count || !ctx->section_addrs[target_idx]) {
			if (ctx->debug >= 1) {
				printf("[rel] Target section not loaded, skipping\n");
			}
			continue;
		}
		
		void* base = ctx->section_addrs[target_idx];
		const Elf32_Shdr* target_sh = &ctx->shdrs[target_idx];
		int is_iram = elf_is_iram_section(target_sh, ctx->shstrtab + target_sh->sh_name);
		
		// Обрабатываем релокации
		const Elf32_Rela* rels = (const Elf32_Rela*)(ctx->elf_data + sh->sh_offset);
		uint32_t count = sh->sh_size / sizeof(Elf32_Rela);
		
		if (ctx->debug >= 1) {
			printf("[rel] Processing %lu entries\n", (unsigned long)count);
		}
		
		for (uint32_t r = 0; r < count; r++) {
			uint32_t r_offset = rels[r].r_offset;
			uint32_t r_info = rels[r].r_info;
			int32_t r_addend = rels[r].r_addend;

			if (ctx->debug >= 1) {
				printf("[rel] Entry %lu: offset=0x%lx, info=0x%lx, addend=%ld\n", (unsigned long)r, (unsigned long)r_offset, (unsigned long)r_info, (long)r_addend);
			}
			
			uint32_t sym_idx = ELF32_R_SYM(r_info);
			uint32_t rel_type = ELF32_R_TYPE(r_info);
			
			void* patch_addr = (uint8_t*)base + r_offset;
			
			// Разрешаем символ
			uint32_t sym_value = 0;
			if (sym_idx != 0) {
				sym_value = elf_resolve_symbol(ctx, sym_idx);
				if (sym_value == 0) {
					printf("[rel] ERROR: Cannot resolve symbol %lu\n", (unsigned long)sym_idx);
					// Продолжаем, но это может вызвать проблемы
				}
			}
			
			// Применяем релокацию
			int err = 0;
			switch (rel_type) {
				case R_XTENSA_NONE:
					break;
					
				case R_XTENSA_32:
					err = apply_r_xtensa_32(ctx, patch_addr, sym_value, r_addend, is_iram);
					break;
					
				case R_XTENSA_SLOT0_OP:
					err = apply_r_xtensa_slot0_op(ctx, patch_addr, sym_value, r_addend);
					break;
					
				default:
					if (ctx->debug >= 1) {
						printf("[rel] WARNING: Unknown reloc type %lu\n", (unsigned long)rel_type);
					}
					break;
			}
			
			if (err != 0) {
				return err;
			}
		}
	}
	
	return 0;
}
