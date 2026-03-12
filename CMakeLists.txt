cmake_minimum_required(VERSION 3.16)

# GCC 14 + gnu++2b can fail in connectedhomeip closure-control sources pulled by
# recent esp_matter on ESP-IDF 5.5.x toolchains.
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS ON)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(xiao_esp32c6_matter_sensor)

# esp_matter currently injects -std=gnu++2b for its target on some versions.
# That overrides the project standard and reintroduces the closure-control error.
# Normalize esp_matter target flags back to gnu++17.
#
# Target name follows ESP-IDF component manager convention: __idf_<namespace>__<name>
#
# NOTE: Only COMPILE_OPTIONS (private to this target's own sources) is touched here.
# DO NOT manipulate INTERFACE_COMPILE_OPTIONS: esp_matter stores generator expressions
# and force-include flags for the CHIP project config (e.g. CHIPProjectConfig.h) in
# that property. Running them through list(FILTER)/set_target_properties corrupts those
# entries and causes CHIP_SYSTEM_CONFIG_USE_LWIP to be undefined in all consumers,
# resulting in a hard #error in connectedhomeip/src/system/SystemConfig.h.
if(TARGET __idf_espressif__esp_matter)
    get_target_property(_esp_matter_compile_opts __idf_espressif__esp_matter COMPILE_OPTIONS)
    if(_esp_matter_compile_opts)
        list(FILTER _esp_matter_compile_opts EXCLUDE REGEX "^-std=gnu\\+\\+2b$")
        list(FILTER _esp_matter_compile_opts EXCLUDE REGEX "^-std=c\\+\\+2b$")
        set_target_properties(__idf_espressif__esp_matter PROPERTIES COMPILE_OPTIONS "${_esp_matter_compile_opts}")
    endif()
    target_compile_options(__idf_espressif__esp_matter PRIVATE -std=gnu++17)
endif()
