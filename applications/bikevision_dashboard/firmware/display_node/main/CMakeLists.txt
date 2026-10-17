# STEP 3: draw camera frames received over ESP-NOW on the TFT.
# New file: display_main.cpp. TFT V1.0 file used as-is: tft.cpp (+ tft.h, font5x7.h).
# tft_image_tests.cpp (V1.0 test app) is kept in this folder but not compiled,
# because it has its own app_main. The original list is in CMakeLists.txt.v1.
idf_component_register(
    SRCS
        "display_main.cpp"
        "tft.cpp"
    INCLUDE_DIRS
        "."
    REQUIRES
        esp_timer
        esp_system
        freertos
        driver
        spiffs
        esp_jpeg
    PRIV_REQUIRES
        espnow_link
)