cmake_minimum_required(VERSION 3.20)
project(MacOsWallPaperDemo LANGUAGES CXX OBJCXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/build")

option(ENABLE_LOGGING "Enable logging" OFF)

add_subdirectory(renderer)
add_subdirectory(ui)
