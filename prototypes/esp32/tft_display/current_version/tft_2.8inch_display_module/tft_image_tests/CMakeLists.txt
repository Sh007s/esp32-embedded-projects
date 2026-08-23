cmake_minimum_required(VERSION 3.16)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

project(tft_display_module)

spiffs_create_partition_image(storage spiffs FLASH_IN_PROJECT)