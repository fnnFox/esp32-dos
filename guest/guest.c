#include "esp_guest.h"

int guest_main(void) {
	
	for (int i = 1; i <= 10; i++) {
		printf("Step %d\n", i);
		delay(500);
	}
	
	return 621;
}
