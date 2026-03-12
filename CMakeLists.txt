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
# Strip the bad flag from both COMPILE_OPTIONS (used when compiling this target's own
# sources) and INTERFACE_COMPILE_OPTIONS (propagated to every consumer of the target,
# including the main component).
if(TARGET __idf_espressif__esp_matter)
    foreach(_prop COMPILE_OPTIONS INTERFACE_COMPILE_OPTIONS)
        get_target_property(_opts __idf_espressif__esp_matter ${_prop})
        if(_opts)
            list(FILTER _opts EXCLUDE REGEX "^-std=(gnu|c)\\+\\+2b$")
            set_target_properties(__idf_espressif__esp_matter PROPERTIES ${_prop} "${_opts}")
        endif()
    endforeach()
    target_compile_options(__idf_espressif__esp_matter PRIVATE -std=gnu++17)
endif()
