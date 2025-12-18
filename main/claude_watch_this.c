#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_heap_caps.h"
#include "esp32/rom/cache.h"
#include <elf.h>

// ============== Типы релокаций Xtensa ==============
#define R_XTENSA_NONE       0
#define R_XTENSA_32         1
#define R_XTENSA_SLOT0_OP   20

// Специальные индексы секций
#define SHN_UNDEF   0
#define SHN_ABS     0xFFF1
#define SHN_COMMON  0xFFF2

// ============== API для гостевого кода ==============
typedef struct {
    int (*_printf)(const char *fmt, ...);
    void (*_delay)(int ms);
} os_api_t;

void _delay(int ms) {
    vTaskDelay(pdMS_TO_TICKS(ms));
}

os_api_t api_table = {
    ._printf = printf,
    ._delay = _delay
};

typedef int (*guest_entry_point_t)(os_api_t*);

typedef struct {
    void* text_mem;
    void* data_mem;
    size_t text_size;
    size_t data_size;
    guest_entry_point_t entry_point;
} loaded_elf_t;

// ============== Таблица экспортируемых символов ==============
// Гостевой код может вызывать эти функции

typedef struct {
    const char* name;
    void* address;
} export_symbol_t;

static const export_symbol_t g_exports[] = {
    // Стандартные функции
    {"printf", (void*)&printf},
    {"puts", (void*)&puts},
    {"putchar", (void*)&putchar},
    {"malloc", (void*)&malloc},
    {"free", (void*)&free},
    {"memcpy", (void*)&memcpy},
    {"memset", (void*)&memset},
    {"strlen", (void*)&strlen},
    {"strcmp", (void*)&strcmp},
    
    // FreeRTOS
    {"vTaskDelay", (void*)&vTaskDelay},
    
    // Конец таблицы
    {NULL, NULL}
};

static void* lookup_export(const char* name) {
    for (const export_symbol_t* sym = g_exports; sym->name != NULL; sym++) {
        if (strcmp(sym->name, name) == 0) {
            printf("DEBUG: Resolved external '%s' -> %p\n", name, sym->address);
            return sym->address;
        }
    }
    printf("DEBUG: WARNING - Unresolved external: '%s'\n", name);
    return NULL;
}

// ============== Запись в IRAM ==============
void elf_iram_write(void *dst, const void *src, size_t len) {
    volatile uint32_t *d_u32 = (volatile uint32_t *)dst;
    const uint8_t *s_u8 = (const uint8_t *)src;

    size_t full_words = len / 4;
    size_t remainder = len % 4;

    for (size_t i = 0; i < full_words; i++) {
        uint32_t word;
        memcpy(&word, s_u8 + (i * 4), 4);
        d_u32[i] = word;
    }

    if (remainder > 0) {
        uint32_t last_word = 0;
        memcpy(&last_word, s_u8 + (full_words * 4), remainder);
        d_u32[full_words] = last_word;
    }
}

// Запись одного слова в IRAM
static void elf_iram_write32(void* dst, uint32_t value) {
    volatile uint32_t* p = (volatile uint32_t*)dst;
    *p = value;
}

// Чтение одного слова из IRAM
static uint32_t elf_iram_read32(void* src) {
    volatile uint32_t* p = (volatile uint32_t*)src;
    return *p;
}

// ============== Классификация секций ==============
int is_iram_section(const Elf32_Shdr* sh, const char* name) {
    if (sh->sh_flags & SHF_EXECINSTR) return 1;
    if (strcmp(name, ".literal") == 0) return 1;
    if (strstr(name, ".literal.") != NULL) return 1;
    return 0;
}

// ============== Контекст загрузки ==============
typedef struct {
    const uint8_t* elf_data;
    const Elf32_Ehdr* hdr;
    const Elf32_Shdr* sh_table;
    const char* sh_str_table;
    const Elf32_Shdr* symtab_sh;
    const Elf32_Shdr* strtab_sh;
    const Elf32_Sym* symtab;
    const char* strtab;
    uint32_t symtab_count;
    void** section_addresses;
} elf_context_t;

// ============== Разрешение символа ==============
static uint32_t resolve_symbol(elf_context_t* ctx, uint32_t sym_idx) {
    if (sym_idx >= ctx->symtab_count) {
        printf("DEBUG: Symbol index %lu out of range\n", (unsigned long)sym_idx);
        return 0;
    }
    
    const Elf32_Sym* sym = &ctx->symtab[sym_idx];
    const char* sym_name = ctx->strtab + sym->st_name;
    uint16_t shndx = sym->st_shndx;
    
    // Отладка
    printf("DEBUG: Resolving symbol [%lu] '%s': shndx=%u, value=0x%lx\n", 
           (unsigned long)sym_idx, sym_name, shndx, (unsigned long)sym->st_value);
    
    // Случай 1: Неопределённый символ (внешний)
    if (shndx == SHN_UNDEF) {
        // Ищем в таблице экспортов
        void* addr = lookup_export(sym_name);
        if (addr) {
            return (uint32_t)addr;
        }
        printf("DEBUG: ERROR - Cannot resolve undefined symbol '%s'\n", sym_name);
        return 0;
    }
    
    // Случай 2: Абсолютный символ
    if (shndx == SHN_ABS) {
        return sym->st_value;
    }
    
    // Случай 3: Символ в секции
    if (shndx < ctx->hdr->e_shnum) {
        void* sec_addr = ctx->section_addresses[shndx];
        if (sec_addr) {
            uint32_t result = (uint32_t)sec_addr + sym->st_value;
            printf("DEBUG: Symbol '%s' resolved to section[%u] + 0x%lx = %p\n",
                   sym_name, shndx, (unsigned long)sym->st_value, (void*)result);
            return result;
        } else {
            printf("DEBUG: ERROR - Symbol '%s' references unloaded section %u\n", 
                   sym_name, shndx);
        }
    }
    
    return 0;
}

// ============== Применение релокации R_XTENSA_SLOT0_OP ==============
// Это для инструкций L32R, CALL и т.д.
static int apply_slot0_reloc(void* addr, uint32_t sym_value, int32_t addend) {
    // L32R имеет формат: 
    // Байты: [op/imm16_lo] [imm16_mid] [imm16_hi/target_reg]
    // Смещение = (imm16 * 4) - 3, относительно ((PC + 3) & ~3)
    
    uint8_t* p = (uint8_t*)addr;
    uint8_t opcode = p[0] & 0x0F;  // Нижние 4 бита
    
    printf("DEBUG: SLOT0_OP at %p, opcode=0x%02x, sym_value=0x%lx, addend=%ld\n",
           addr, opcode, (unsigned long)sym_value, (long)addend);
    
    // Для L32R (opcode 0x01 в slot 0)
    // Мы просто вычисляем целевой адрес и патчим literal
    // Но literal уже должен быть пропатчен через R_XTENSA_32
    
    // На самом деле R_XTENSA_SLOT0_OP обычно нужен для CALL
    // Пока просто логируем и пропускаем
    printf("DEBUG: SLOT0_OP relocation - currently skipped (may need implementation)\n");
    
    return 0;
}

// ============== Главная функция загрузки ==============
int load_elf_image(const uint8_t* elf_data, loaded_elf_t* out_module) {
    memset(out_module, 0, sizeof(*out_module));
    
    elf_context_t ctx = {0};
    ctx.elf_data = elf_data;
    ctx.hdr = (const Elf32_Ehdr*)elf_data;
    
    // Проверка магического числа
    if (memcmp(ctx.hdr->e_ident, "\177ELF", 4) != 0) {
        printf("DEBUG: Invalid ELF magic\n");
        return -1;
    }
    
    // Проверка архитектуры
    if (ctx.hdr->e_machine != EM_XTENSA) {
        printf("DEBUG: Not Xtensa architecture: %d\n", ctx.hdr->e_machine);
        return -1;
    }
    
    ctx.sh_table = (const Elf32_Shdr*)(elf_data + ctx.hdr->e_shoff);
    ctx.sh_str_table = (const char*)(elf_data + ctx.sh_table[ctx.hdr->e_shstrndx].sh_offset);
    
    // ============ Этап 1: Подсчёт размеров ============
    size_t total_iram_size = 0;
    size_t total_dram_size = 0;
    
    for (int i = 0; i < ctx.hdr->e_shnum; i++) {
        const Elf32_Shdr* sh = &ctx.sh_table[i];
        const char* name = ctx.sh_str_table + sh->sh_name;
        
        if (!(sh->sh_flags & SHF_ALLOC)) continue;
        if (sh->sh_size == 0) continue;
        
        size_t aligned_size = (sh->sh_size + 3) & ~3;
        
        if (is_iram_section(sh, name)) {
            total_iram_size += aligned_size;
        } else {
            total_dram_size += aligned_size;
        }
    }
    
    printf("DEBUG: Total IRAM needed: %d bytes\n", total_iram_size);
    printf("DEBUG: Total DRAM needed: %d bytes\n", total_dram_size);
    
    // ============ Этап 2: Выделение памяти ============
    uint8_t* iram_block = NULL;
    uint8_t* dram_block = NULL;
    
    if (total_iram_size > 0) {
        iram_block = heap_caps_malloc(total_iram_size, MALLOC_CAP_EXEC | MALLOC_CAP_32BIT);
        if (!iram_block) {
            printf("DEBUG: Failed to allocate IRAM\n");
            return -2;
        }
        out_module->text_mem = iram_block;
        out_module->text_size = total_iram_size;
    }
    
    if (total_dram_size > 0) {
        dram_block = heap_caps_malloc(total_dram_size, MALLOC_CAP_8BIT);
        if (!dram_block) {
            printf("DEBUG: Failed to allocate DRAM\n");
            if (iram_block) heap_caps_free(iram_block);
            return -2;
        }
        memset(dram_block, 0, total_dram_size);
        out_module->data_mem = dram_block;
        out_module->data_size = total_dram_size;
    }
    
    ctx.section_addresses = calloc(ctx.hdr->e_shnum, sizeof(void*));
    if (!ctx.section_addresses) {
        if (iram_block) heap_caps_free(iram_block);
        if (dram_block) heap_caps_free(dram_block);
        return -4;
    }
    
    // ============ Этап 3: Загрузка секций ============
    printf("DEBUG: Loading sections...\n");
    size_t iram_offset = 0;
    size_t dram_offset = 0;
    
    for (int i = 0; i < ctx.hdr->e_shnum; i++) {
        const Elf32_Shdr* sh = &ctx.sh_table[i];
        const char* name = ctx.sh_str_table + sh->sh_name;
        
        // Находим таблицы символов
        if (sh->sh_type == SHT_SYMTAB) {
            ctx.symtab_sh = sh;
            ctx.strtab_sh = &ctx.sh_table[sh->sh_link];
            ctx.symtab = (const Elf32_Sym*)(elf_data + sh->sh_offset);
            ctx.strtab = (const char*)(elf_data + ctx.strtab_sh->sh_offset);
            ctx.symtab_count = sh->sh_size / sizeof(Elf32_Sym);
            printf("DEBUG: Found SYMTAB with %lu symbols\n", (unsigned long)ctx.symtab_count);
        }
        
        // Пропускаем нераспределяемые секции
        if (!(sh->sh_flags & SHF_ALLOC)) continue;
        if (sh->sh_size == 0) {
            printf("DEBUG: [%d] %s (empty, skipped)\n", i, name);
            continue;
        }
        
        size_t aligned_size = (sh->sh_size + 3) & ~3;
        
        if (is_iram_section(sh, name)) {
            void* dest = iram_block + iram_offset;
            elf_iram_write(dest, elf_data + sh->sh_offset, sh->sh_size);
            ctx.section_addresses[i] = dest;
            iram_offset += aligned_size;
            printf("DEBUG: [%d] %-20s -> IRAM %p (size %lu)\n", i, name, dest, (unsigned long)sh->sh_size);
        } else {
            void* dest = dram_block + dram_offset;
            if (sh->sh_type == SHT_NOBITS) {
                // .bss - уже обнулено
            } else {
                memcpy(dest, elf_data + sh->sh_offset, sh->sh_size);
            }
            ctx.section_addresses[i] = dest;
            dram_offset += aligned_size;
            printf("DEBUG: [%d] %-20s -> DRAM %p (size %lu)\n", i, name, dest, (unsigned long)sh->sh_size);
        }
    }
    
    // ============ Этап 4: Релокации ============
    printf("DEBUG: Processing relocations...\n");
    
    for (int i = 0; i < ctx.hdr->e_shnum; i++) {
        const Elf32_Shdr* sh = &ctx.sh_table[i];
        
        if (sh->sh_type != SHT_RELA) continue;
        
        const char* name = ctx.sh_str_table + sh->sh_name;
        int target_sec_idx = sh->sh_info;
        
        printf("DEBUG: RELA section [%d] '%s' -> target section [%d]\n", i, name, target_sec_idx);
        
        void* base = ctx.section_addresses[target_sec_idx];
        if (!base) {
            printf("DEBUG: Target section [%d] not loaded, skipping\n", target_sec_idx);
            continue;
        }
        
        const Elf32_Rela* rels = (const Elf32_Rela*)(elf_data + sh->sh_offset);
        int rel_count = sh->sh_size / sizeof(Elf32_Rela);
        
        printf("DEBUG: Processing %d relocations...\n", rel_count);
        
        for (int r = 0; r < rel_count; r++) {
            uint32_t r_offset = rels[r].r_offset;
            uint32_t r_info = rels[r].r_info;
            int32_t r_addend = rels[r].r_addend;
            
            uint32_t sym_idx = ELF32_R_SYM(r_info);
            uint32_t rel_type = ELF32_R_TYPE(r_info);
            
            void* patch_addr = (uint8_t*)base + r_offset;
            
            printf("DEBUG: Reloc[%d]: type=%lu, offset=0x%lx, sym=%lu, addend=%ld\n",
                   r, (unsigned long)rel_type, (unsigned long)r_offset, 
                   (unsigned long)sym_idx, (long)r_addend);
            
            switch (rel_type) {
                case R_XTENSA_NONE:
                    // Ничего не делаем
                    break;
                    
                case R_XTENSA_32: {
                    // Простая 32-битная релокация
                    uint32_t sym_value = resolve_symbol(&ctx, sym_idx);
                    if (sym_value == 0 && sym_idx != 0) {
                        printf("DEBUG: ERROR - Failed to resolve symbol %lu\n", (unsigned long)sym_idx);
                        // Продолжаем, но это ошибка
                    }
                    
                    uint32_t final_value = sym_value + r_addend;
                    
                    // Определяем, IRAM это или DRAM
                    if (is_iram_section(&ctx.sh_table[target_sec_idx], 
                                        ctx.sh_str_table + ctx.sh_table[target_sec_idx].sh_name)) {
                        elf_iram_write32(patch_addr, final_value);
                    } else {
                        *(uint32_t*)patch_addr = final_value;
                    }
                    
                    printf("DEBUG: PATCHED R_XTENSA_32 at %p: -> 0x%08lx\n", 
                           patch_addr, (unsigned long)final_value);
                    break;
                }
                
                case R_XTENSA_SLOT0_OP: {
                    // Релокация для инструкций (CALL, L32R, etc.)
                    uint32_t sym_value = resolve_symbol(&ctx, sym_idx);
                    apply_slot0_reloc(patch_addr, sym_value, r_addend);
                    break;
                }
                
                default:
                    printf("DEBUG: WARNING - Unhandled relocation type %lu\n", (unsigned long)rel_type);
                    break;
            }
        }
    }
    
    // ============ Этап 5: Поиск точки входа ============
    printf("DEBUG: Looking for entry point 'guest_main'...\n");
    
    out_module->entry_point = NULL;
    
    if (ctx.symtab && ctx.strtab) {
        for (uint32_t i = 0; i < ctx.symtab_count; i++) {
            const Elf32_Sym* sym = &ctx.symtab[i];
            const char* sym_name = ctx.strtab + sym->st_name;
            
            if (strcmp(sym_name, "guest_main") == 0) {
                uint16_t shndx = sym->st_shndx;
                
                if (shndx != SHN_UNDEF && shndx < ctx.hdr->e_shnum) {
                    void* sec_addr = ctx.section_addresses[shndx];
                    if (sec_addr) {
                        out_module->entry_point = (guest_entry_point_t)((uint32_t)sec_addr + sym->st_value);
                        printf("DEBUG: Entry point 'guest_main' found at %p\n", out_module->entry_point);
                    }
                }
                break;
            }
        }
    }
    
    if (!out_module->entry_point) {
        printf("DEBUG: ERROR - 'guest_main' not found!\n");
        free(ctx.section_addresses);
        return -5;
    }
    
    free(ctx.section_addresses);
    
    // ============ Этап 6: Сброс кэша ============
    Cache_Flush(0);
#if CONFIG_ESP32_IRAM_AS_8BIT_ACCESSIBLE_MEMORY
    Cache_Flush(1);
#endif
    
    printf("DEBUG: ELF loaded successfully!\n");
    return 0;
}

// ============== Главная функция ==============
void app_main(void) {
    setvbuf(stdin, NULL, _IONBF, 0);
    setvbuf(stdout, NULL, _IONBF, 0);
    
    printf("ELF Loader Ready.\n");
    printf("Free IRAM: %d bytes\n", heap_caps_get_free_size(MALLOC_CAP_EXEC));
    printf("Free DRAM: %d bytes\n", heap_caps_get_free_size(MALLOC_CAP_8BIT));
    printf("Waiting for binary data...\n");
    
    // Ждём начала передачи
    while (getchar() != EOF) {
        vTaskDelay(pdMS_TO_TICKS(1));
    }
    printf("^\n");
    fflush(stdout);
    
    size_t BUFFER_SIZE = 32768;
    uint8_t *temp_buffer = malloc(BUFFER_SIZE);
    if (!temp_buffer) {
        printf("Error: Failed to allocate temp buffer.\n");
        return;
    }
    
    int received_len = 0;
    int timeout_ticks = 0;
    const int MAX_TIMEOUT = 50;
    
    while (1) {
        int c = getchar();
        
        if (c != EOF) {
            timeout_ticks = 0;
            temp_buffer[received_len++] = (uint8_t)c;
            if (received_len >= BUFFER_SIZE) {
                printf("Error: Memory limit reached.\n");
                break;
            }
        } else {
            if (received_len > 0) {
                timeout_ticks++;
                vTaskDelay(pdMS_TO_TICKS(10));
                if (timeout_ticks > MAX_TIMEOUT) {
                    break;
                }
            } else {
                vTaskDelay(pdMS_TO_TICKS(100));
            }
        }
    }
    
    printf("\nReceived %d bytes. Parsing ELF...\n", received_len);
    
    loaded_elf_t guest = {0};
    int err = load_elf_image(temp_buffer, &guest);
    
    if (err == 0) {
        printf("ELF Loaded successfully!\n");
        printf("Text (IRAM): %p (%d bytes)\n", guest.text_mem, guest.text_size);
        printf("Data (DRAM): %p (%d bytes)\n", guest.data_mem, guest.data_size);
        printf("Entry Point: %p\n", guest.entry_point);
        
        // Дамп IRAM
        printf("IRAM dump:\n");
        volatile uint32_t* p = (volatile uint32_t*)guest.text_mem;
        for (int i = 0; i < 16 && i * 4 < guest.text_size; i++) {
            printf("  [%p] = %08lX\n", &p[i], (unsigned long)p[i]);
        }
        
        printf("\nJumping to guest code...\n");
        printf("-------------------------\n");
        
        int result = guest.entry_point(&api_table);
        
        printf("-------------------------\n");
        printf("Guest returned: %d\n", result);
    } else {
        printf("Error loading ELF: %d\n", err);
    }
    
    free(temp_buffer);
    
    if (guest.text_mem) heap_caps_free(guest.text_mem);
    if (guest.data_mem) heap_caps_free(guest.data_mem);
    
    printf("Done.\n");
}
