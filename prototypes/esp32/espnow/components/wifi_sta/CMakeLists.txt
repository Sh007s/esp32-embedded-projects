idf_component_register(
    SRCS "wifi_sta.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_wifi esp_event
    PRIV_REQUIRES esp_netif nvs_flash
)
