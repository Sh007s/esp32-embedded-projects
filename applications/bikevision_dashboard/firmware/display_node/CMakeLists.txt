cmake_minimum_required(VERSION 3.16)

set(EXTRA_COMPONENT_DIRS "${CMAKE_CURRENT_LIST_DIR}/../components")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

project(tft_display_module)

spiffs_create_partition_image(storage spiffs FLASH_IN_PROJECT)