cmake_minimum_required(VERSION 3.16)

# Shared components (wifi_sta, espnow_link, link_test) live one folder up,
# so other projects can reuse them the same way.
set(EXTRA_COMPONENT_DIRS "${CMAKE_CURRENT_LIST_DIR}/../components")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(sender_node)
