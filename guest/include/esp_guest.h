#ifndef ESP_GUEST_H
#define ESP_GUEST_H

/* ============== Типы ============== */

typedef unsigned int size_t;
typedef int int32_t;
typedef unsigned int uint32_t;
typedef short int16_t;
typedef unsigned short uint16_t;
typedef signed char int8_t;
typedef unsigned char uint8_t;

#define NULL ((void*)0)
#define bool _Bool
#define true 1
#define false 0

/* ============== Стандартный вывод ============== */

extern int printf(const char* fmt, ...);
extern int sprintf(char* buf, const char* fmt, ...);
extern int snprintf(char* buf, size_t size, const char* fmt, ...);
extern int puts(const char* s);
extern int putchar(int c);

/* ============== Память ============== */

extern void* malloc(size_t size);
extern void free(void* ptr);
extern void* calloc(size_t num, size_t size);
extern void* realloc(void* ptr, size_t size);
extern void* memcpy(void* dst, const void* src, size_t n);
extern void* memset(void* dst, int c, size_t n);
extern void* memmove(void* dst, const void* src, size_t n);
extern int memcmp(const void* a, const void* b, size_t n);

/* ============== Строки ============== */

extern size_t strlen(const char* s);
extern int strcmp(const char* a, const char* b);
extern int strncmp(const char* a, const char* b, size_t n);
extern char* strcpy(char* dst, const char* src);
extern char* strncpy(char* dst, const char* src, size_t n);
extern char* strcat(char* dst, const char* src);
extern char* strchr(const char* s, int c);
extern char* strstr(const char* haystack, const char* needle);

/* ============== FreeRTOS ============== */

extern void delay(uint32_t ms);

/* ============== Разное ============== */

extern int rand(void);
extern void srand(unsigned int seed);
extern int abs(int x);

#endif /* ESP_GUEST_H */
