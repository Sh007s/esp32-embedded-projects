idf_component_register(
    SRCS
        "main.c"
        "camera.c"
        "sdcard.c"
        "wifi.c"
        "http_server.c"
    INCLUDE_DIRS "."
    PRIV_REQUIRES
        esp_wifi
        esp_netif
        esp_event
        nvs_flash
        esp_http_server
        fatfs
)