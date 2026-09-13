cmake_minimum_required(VERSION 3.16.0)

# Reuse the ESP-SR release shipped with the required Waveshare reference.
# Keeping it as an ESP-IDF component is important: its CMake file packs and
# flashes the selected speech model into the dedicated `model` partition.
# These paths also make ESP-IDF track the vendor component metadata whenever
# the speech integration patch changes.
list(APPEND EXTRA_COMPONENT_DIRS
    "${CMAKE_SOURCE_DIR}/.vendor-reference/example/ESP-IDF-5.3.2/ESP32-S3-Touch-LCD-1.46-Test/components/espressif__esp-dsp"
    "${CMAKE_SOURCE_DIR}/.vendor-reference/example/ESP-IDF-5.3.2/ESP32-S3-Touch-LCD-1.46-Test/components/espressif__esp-sr"
)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(chronvs)
