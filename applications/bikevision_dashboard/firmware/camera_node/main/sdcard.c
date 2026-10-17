#include "sdcard.h"

#include <stdio.h>

#include "esp_log.h"
#include "esp_vfs_fat.h"

#include "driver/gpio.h"
#include "driver/sdmmc_host.h"
#include "driver/sdmmc_defs.h"
#include "sdmmc_cmd.h"

static const char *TAG = "SDCARD";

/* ESP32-CAM (AI Thinker) flash LED. In 4-bit SD mode this pin is SD DATA1,
 * which made the LED flicker on every SD access. */
#define FLASH_LED_GPIO GPIO_NUM_4

esp_err_t sdcard_init(void)
{
    ESP_LOGI(TAG, "Initializing SD card...");

    sdmmc_host_t host = SDMMC_HOST_DEFAULT();

    sdmmc_slot_config_t slot_config = SDMMC_SLOT_CONFIG_DEFAULT();

    /*
     * 1-bit mode: uses only CLK (GPIO14), CMD (GPIO15) and DATA0 (GPIO2).
     * Frees GPIO4 (flash LED), GPIO12 (boot strapping pin) and GPIO13.
     * Revert by removing this line if the card fails to mount.
     */
    slot_config.width = 1;

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

    ESP_LOGI(TAG, "SD card mounted successfully! (1-bit mode)");

    /* GPIO4 is no longer an SD data line: drive it low so the flash LED stays off. */
    gpio_reset_pin(FLASH_LED_GPIO);
    gpio_set_direction(FLASH_LED_GPIO, GPIO_MODE_OUTPUT);
    gpio_set_level(FLASH_LED_GPIO, 0);

    sdmmc_card_print_info(stdout, card);

    return ESP_OK;
}