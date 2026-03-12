cmake_minimum_required(VERSION 3.16)

# GCC 14 + gnu++2b can fail in connectedhomeip closure-control sources pulled by
# recent esp_matter on ESP-IDF 5.5.x toolchains.
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS ON)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(xiao_esp32c6_matter_sensor)

# esp_matter injects -std=gnu++2b on some toolchain/version combinations.
# GCC 14 + gnu++2b fails to compile certain connectedhomeip closure-control sources.
#
# FIX: append -std=gnu++17 AFTER all existing flags via target_compile_options.
# GCC processes -std= left-to-right and uses the LAST value, so this overrides
# any injected -std=gnu++2b without touching the existing COMPILE_OPTIONS property.
#
# WHY NOT use get_target_property / list(FILTER) / set_target_properties:
# esp_matter stores its CHIPProjectConfig.h force-include inside COMPILE_OPTIONS
# using generator expressions that contain internal semicolons, e.g.:
#   $<$<COMPILE_LANGUAGE:CXX>:-std=gnu++2b;-include /path/CHIPProjectConfig.h>
# CMake's list() splits on ALL semicolons, so reading the property and setting it
# back corrupts those entries - the force-include that defines
# CHIP_SYSTEM_CONFIG_USE_LWIP is silently lost, causing a hard #error in
# connectedhomeip/src/system/SystemConfig.h at every compilation unit.
if(TARGET __idf_espressif__esp_matter)
    target_compile_options(__idf_espressif__esp_matter PRIVATE -std=gnu++17)
endif()
