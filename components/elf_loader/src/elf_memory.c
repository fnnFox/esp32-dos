#include "elf_specific.h"
#include <string.h>

void elf_iram_memcpy(void* dst, const void* src, size_t len) {
	volatile uint32_t* d = (volatile uint32_t*)dst;
	const uint8_t* s = (const uint8_t*)src;

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

void elf_iram_memset(void* dst, int val, size_t len) {
	volatile uint32_t* d = (volatile uint32_t*)dst;
	uint32_t word = val | (val << 8) | (val << 16) | (val << 24);
	
	size_t words = (len + 3) / 4;
	for (size_t i = 0; i < words; i++) {
		d[i] = word;
	}
}

void elf_write32(void* dst, uint32_t value, int is_iram) {
	if (is_iram) {
		volatile uint32_t* p = (volatile uint32_t*)dst;
		*p = value;
	} else {
		*(uint32_t*)dst = value;
	}
}

uint32_t elf_read32(void* src, int is_iram) {
	if (is_iram) {
		volatile uint32_t* p = (volatile uint32_t*)src;
		return *p;
	} else {
		return *(uint32_t*)src;
	}
}

