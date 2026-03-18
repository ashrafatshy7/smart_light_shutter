# The following five lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.16)

if("$ENV{IDF_PATH}" STREQUAL "")
    set(ENV{IDF_PATH} "/Users/ashrafatshy/.espressif/v5.5.3/esp-idf")
endif()


include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(smart_light)
