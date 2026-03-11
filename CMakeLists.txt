cmake_minimum_required(VERSION 3.16)

# GCC 14 + gnu++2b can fail in connectedhomeip closure-control sources
# pulled by recent esp_matter. Force a project-wide C++ standard compatible
# with current esp-matter/CHIP sources on ESP-IDF 5.5.2.
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS ON)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(xiao_esp32c6_matter_sensor)
