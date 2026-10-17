# STEP 2 (link test): only the temporary link-test app is compiled.
# The TFT V1.0 files (tft.cpp, tft.h, font5x7.h, tft_image_tests.cpp) stay in
# this folder untouched; the original CMakeLists is kept as CMakeLists.txt.v1.
idf_component_register(
    SRCS "display_link_test.c"
    INCLUDE_DIRS "."
    PRIV_REQUIRES espnow_link esp_timer
)
