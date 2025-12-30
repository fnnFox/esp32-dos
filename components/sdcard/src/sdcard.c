#include "driver/spi_common.h"
#include "driver/sdspi_host.h"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"

#define MOUNT_POINT "/sd"

#define PIN_MOSI	GPIO_NUM_23
#define PIN_MISO	GPIO_NUM_19
#define PIN_CLK		GPIO_NUM_18
#define PIN_CS		GPIO_NUM_5

static sdmmc_card_t *card = NULL;
static bool mounted = false;

esp_err_t sdcard_init(void) {

	spi_bus_config_t bus_cfg = {
		.mosi_io_num = PIN_MOSI,
		.miso_io_num = PIN_MISO,
		.sclk_io_num = PIN_CLK,
		.quadwp_io_num = -1,
		.quadhd_io_num = -1,
		.max_transfer_sz = 4000
	};

	esp_err_t err = spi_bus_initialize(SPI2_HOST, &bus_cfg, SPI_DMA_CH_AUTO);
	if (err != ESP_OK) {
		printf("[sdc] SPI bus init failed: %s\n", esp_err_to_name(err));
		return err;
	}

	esp_vfs_fat_sdmmc_mount_config_t mount_cfg = {
		.format_if_mount_failed = false,
		.max_files = 5,
		.allocation_unit_size = 16 * 1024
	};

	sdmmc_host_t host = SDSPI_HOST_DEFAULT();
	host.slot = SPI2_HOST;

	sdspi_device_config_t slot_cfg = SDSPI_DEVICE_CONFIG_DEFAULT();
	slot_cfg.gpio_cs = PIN_CS;
	slot_cfg.host_id = SPI2_HOST;

	err = esp_vfs_fat_sdspi_mount(MOUNT_POINT, &host, &slot_cfg, &mount_cfg, &card);
	if (err != ESP_OK) {
		printf("[sdc] Mount failed: %s\n", esp_err_to_name(err));
		return err;
	}

	mounted = true;
	printf("[sdc] Mounted at %s\n", MOUNT_POINT);
	sdmmc_card_print_info(stdout, card);

	return ESP_OK;
}

void sdcard_deinit(void) {
	if (!mounted) {
		return;
	}

	esp_vfs_fat_sdcard_unmount(MOUNT_POINT, card);
	spi_bus_free(SPI2_HOST);
	card = NULL;
	mounted = false;
	printf("[sdc] Unmounted\n");
}

bool sdcard_is_mounted(void) {
	return mounted;
}

const char* sdcard_get_mount_point(void) {
	return MOUNT_POINT;
}

esp_err_t sdcard_read_file(const char* path, uint8_t** out_data, size_t* out_size) {
	if (!mounted) {
		printf("[sdc] Not mounted\n");
		return ESP_ERR_INVALID_STATE;
	}

	FILE* f = fopen(path, "rb");
	if (!f) {
		printf("[sdc] Failed to open: %s\n", path);
		return ESP_ERR_NOT_FOUND;
	}

	fseek(f, 0, SEEK_END);
	size_t size = ftell(f);
	fseek(f, 0, SEEK_SET);

	uint8_t* data = malloc(size);
	if (!data) {
		printf("[sdc] Failed to allocate %u bytes\n", size);
		fclose(f);
		return ESP_ERR_NO_MEM;
	}

	size_t read = fread(data, 1, size, f);
	fclose(f);

	if (read != size) {
		printf("[sdc] Read error: Invalid size: %u/%u bytes\n", read, size);
		free(data);
		return ESP_ERR_INVALID_SIZE;
	}

	printf("[sdc] Loaded %s (%u bytes)\n", path, size);
	*out_data = data;
	*out_size = size;

	return ESP_OK;
}

