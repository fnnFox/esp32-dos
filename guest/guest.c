//#include "esp_guest.h"

int module_main(void) {

	const char* str = "testing!";
	
	int d = 1;

	for (int i = 1; i <= 10; i++) {
		// printf("Step %d\n", i);
		// delay(500);
		d += i * i;
	}

	return str[0];
}
