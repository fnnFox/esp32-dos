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

uint32_t elf_read32(void* src) {
	return *(uint32_t*)src;
}

void elf_write24(void *dst, uint32_t value, int is_iram) {
	uint8_t bytes[3] = {
		value & 0xFF,
		(value >> 8) & 0xFF,
		(value >> 16) & 0xFF
	};

	if (is_iram) {
		elf_iram_memcpy(dst, bytes, 3);
	} else {
		memcpy(dst, bytes, 3);
	}
}

uint32_t elf_read24(void *src) {
	uint8_t* p = (uint8_t*)src;
	return p[0] | (p[1] << 8) | (p[2] << 16);
}

