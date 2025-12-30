#ifndef DOS_SDCARD_H
#define DOS_SDCARD_H

#include "esp_err.h"
#include <stdbool.h>

esp_err_t sdcard_init(void);
void sdcard_deinit(void);
bool sdcard_is_mounted(void);
const char* sdcard_get_mount_point(void);
esp_err_t sdcard_read_file(const char* path, uint8_t** out_data, size_t* out_size);

#endif
