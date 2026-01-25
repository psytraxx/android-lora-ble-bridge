cmake_minimum_required(VERSION 3.16.0)
set(EXCLUDE_COMPONENTS espressif__esp_insights espressif__esp_diagnostics)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(firmware)
