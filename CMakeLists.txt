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

# This board uses the SPD2010 over QSPI, not the ESP32's parallel RGB bus.
# ESP-IDF 5.3.1 includes this unused driver based on SoC capability alone;
# GCC 13.2.0 crashes in its register allocator in our no-log debug build.
# Remove only that source, retaining the SPI/QSPI transport and vendor panel.
idf_component_get_property(chronvs_lcd_library esp_lcd COMPONENT_LIB)
get_target_property(chronvs_lcd_sources ${chronvs_lcd_library} SOURCES)
list(FILTER chronvs_lcd_sources EXCLUDE REGEX "(^|/)rgb/esp_lcd_panel_rgb\\.c$")
set_property(TARGET ${chronvs_lcd_library} PROPERTY SOURCES ${chronvs_lcd_sources})
