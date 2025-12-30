#include "esp_guest.h"

void cntr(char* str) {
	for (int i = 0; i < 10; i++) {
		printf("%s - %d\n", str, i);
	}
}

int guest_main(int argc, char** argv) {

	if (argc < 2) {
		printf("no arguments\n");
		return -1;
	}

	printf("hello from module!\nargv[1] = %s\n", argv[1]);
	cntr(argv[1]);

	return 0;
}
