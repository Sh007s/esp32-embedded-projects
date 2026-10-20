# espnow_link: camera -> display frame link.
# espnow_link.c = ESP-NOW (BV_LINK_MODE 0), wifi_link.c = Wi-Fi direct (BV_LINK_MODE 1).
# Both are always listed; each file compiles to nothing unless its mode is
# selected in include/bv_link_config.h.
idf_component_register(
    SRCS "espnow_link.c" "wifi_link.c"
    INCLUDE_DIRS "include"
    PRIV_REQUIRES esp_wifi esp_event esp_timer nvs_flash esp_rom esp_netif lwip
)
