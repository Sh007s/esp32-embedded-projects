idf_component_register(
    SRCS "espnow_link.c"
    INCLUDE_DIRS "include"
    PRIV_REQUIRES esp_wifi esp_event esp_timer nvs_flash esp_rom
)
