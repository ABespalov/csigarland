cmake_minimum_required(VERSION 3.16)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
# Written by scripts/firmware_build_version.py; carries the esp_app_desc version.
include(${CMAKE_CURRENT_LIST_DIR}/generated/project_ver.cmake OPTIONAL)
project(csigarland)
