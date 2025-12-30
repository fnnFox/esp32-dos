#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_heap_caps.h"

#include "uart_receiver.h"
#include "elf_loader.h"
#include "shell.h"
#include "sdcard.h"

#include <dirent.h>

typedef struct {
	uint8_t* loaded_data;
	size_t loaded_size;
	elf_module_t module;
} dos_context_t;
dos_context_t dos_context = {0};

static void dump_memory(const char* label, void* addr, size_t size) {
	printf("%s at %p:\n", label, addr);
	volatile uint32_t* p = (volatile uint32_t*)addr;
	size_t words = (size + 3) / 4;
	if (words > 16) words = 16;
	
	for (size_t i = 0; i < words; i++) {
		printf("  [%p] = %08lX\n", &p[i], (unsigned long)p[i]);
	}
}

void free_data() {
	if (dos_context.loaded_data) {
		free(dos_context.loaded_data);
		dos_context.loaded_data = NULL;
		dos_context.loaded_size = 0;
		printf("Data freed.\n");
	}
}

void load_data() {

	free_data();
	
	dos_context.loaded_data = uart_receive_data(&dos_context.loaded_size);
	if (dos_context.loaded_size != 0) {
		printf("Data loaded. Received %d bytes.\n", dos_context.loaded_size);
	} else {
		printf("Error: No data received.\n");
	}
}

void read_data(int argc, char** argv) {
	free_data();
	esp_err_t err = sdcard_read_file(argv[1], &dos_context.loaded_data, &dos_context.loaded_size);
	if (err != ESP_OK) {
		printf("Data read error\n");
	}
}

void ls(int argc, char** argv) {
	const char* path = (argc > 1) ? argv[1] : "/sd";

	DIR* dir = opendir(path);
	if (!dir) {
		printf("No such directory: %s\n", path);
		return;
	}

	struct dirent* entry;
	while ((entry = readdir(dir)) != NULL) {
		printf(" %s\n", entry->d_name);
	}

	closedir(dir);
}

void unload_module() {
	elf_unload(&dos_context.module);
	printf("Module unloaded.\n");
}

void load_module() {
	if (!dos_context.loaded_size) {
		printf("Error: No loaded module.\n");
		return;
	}
	elf_unload(&dos_context.module);

	int err = elf_load(dos_context.loaded_data, dos_context.loaded_size, &dos_context.module);
	if (err != ELF_OK) {
		printf("Error loading ELF: %s\n", elf_strerror(err));
		return;
	}

	printf("\n");
	printf("=== Module loaded ===\n");
	printf("Text: %p (%d bytes)\n", dos_context.module.text_mem, dos_context.module.text_size);
	printf("Data: %p (%d bytes)\n", dos_context.module.data_mem, dos_context.module.data_size);
	printf("Entry: %p\n", dos_context.module.entry_point);
	printf("\n");
}

void run_module(int argc, char**  argv) {
	if (!dos_context.module.entry_point) {
		printf("Error: Module not loaded.\n");
		return;
	}

	// printf("Jumping to module code...\n");
	// printf("--------------------------------\n");
	typedef int (*entry_func_t)(int,char**);
	int result = ((entry_func_t)dos_context.module.entry_point)(argc, argv);
	// printf("--------------------------------\n");
	printf("\nModule returned with code: %d\n", result);
}

void app_main(void) {

	printf("\033[2J\033[H");

	uart_receiver_init();

	char buf[64];
	int iram = heap_caps_get_free_size(MALLOC_CAP_EXEC);
	int dram = heap_caps_get_free_size(MALLOC_CAP_8BIT);
	printf("\n");
	printf("================================\n");
	printf("|        ESP32-DOS v0.1        |\n");
	printf("|------------------------------|\n");
	snprintf(buf, sizeof(buf), " Free IRAM: %d bytes", iram);
	printf("|%-30s|\n", buf);
	snprintf(buf, sizeof(buf), " Free DRAM: %d bytes", dram);
	printf("|%-30s|\n", buf);
	printf("================================\n\n");

	sdcard_init();

	char line[128];
	char* argv[8];
	while(1) {

		printf("SHELL > ");
		fflush(stdout);

		int len = shell_read_line(line, sizeof(line));
		if (!len) continue;

		int argc = shell_parse_args(line, argv);
		if (!argc) continue;

		if (strcmp(argv[0], "load") == 0) {
			load_data();
			continue;
		}
		if (strcmp(argv[0], "ls") == 0) {
			ls(argc, argv);
			continue;
		}
		if (strcmp(argv[0], "read") == 0) {
			read_data(argc, argv);
			continue;
		}
		if (strcmp(argv[0], "module") == 0) {
			load_module();
			continue;
		}
		if (strcmp(argv[0], "run") == 0) {
			run_module(argc , argv);
			continue;
		}
		if (strcmp(argv[0], "exit") == 0) {
			break;
		}
		printf("Error: No such command.\n");
	}
	// if (guest.text_mem) {
	// 	dump_memory("IRAM", guest.text_mem, guest.text_size);
	// }
	// if (guest.data_mem) {
	// 	dump_memory("DRAM", guest.data_mem, guest.data_size);
	// }

	printf("System stopped.\n");
}
