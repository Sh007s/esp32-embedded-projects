#include "sdcard.h"

#include <stdio.h>

#include "esp_log.h"
#include "esp_vfs_fat.h"

#include "driver/sdmmc_host.h"
#include "driver/sdmmc_defs.h"
#include "sdmmc_cmd.h"

static const char *TAG = "SDCARD";

esp_err_t sdcard_init(void)
{
    ESP_LOGI(TAG, "Initializing SD card...");

    sdmmc_host_t host = SDMMC_HOST_DEFAULT();

    sdmmc_slot_config_t slot_config = SDMMC_SLOT_CONFIG_DEFAULT();

    esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = false,
        .max_files = 5,
        .allocation_unit_size = 16 * 1024
    };

    sdmmc_card_t *card;

    esp_err_t ret = esp_vfs_fat_sdmmc_mount("/sdcard",
                                            &host,
                                            &slot_config,
                                            &mount_config,
                                            &card);

    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to mount SD card (%s)",
                 esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "SD card mounted successfully!");

    sdmmc_card_print_info(stdout, card);

    return ESP_OK;
}