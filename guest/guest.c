#include "esp_guest.h"

int counter;

int guest_main(void) {

	const char* test = "hello!";
	while (counter < 10) {
		counter++;
	}

	return test[0];
}
