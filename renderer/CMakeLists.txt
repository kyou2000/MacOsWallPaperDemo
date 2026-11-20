cmake_minimum_required(VERSION 3.20)
project(WallPaperRenderer LANGUAGES CXX OBJCXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/build")

set(METALCPP_HOME $ENV{METALCPP_HOME})
set(SDL_HOME $ENV{SDL_HOME})
set(STB_PATH $ENV{STB_PATH})


message(STATUS "METALCPP_HOME : ${METALCPP_HOME}")
message(STATUS "SDL_HOME : ${SDL_HOME}")
message(STATUS "STB_PATH : ${STB_PATH}")

if(NOT EXISTS "${SDL_HOME}/build/libSDL3.dylib")
    message(FATAL_ERROR "SDL3 library not found: ${SDL_HOME}/build/libSDL3.dylib")
endif()

add_executable(WallPaperRenderer
    rendermain.cpp
    src/application.cpp
    src/background.mm
    src/backgroundrender.cpp
    src/framesolt.cpp
    src/render.cpp
    src/utils.cpp
    src/videorender.mm
)

target_include_directories(WallPaperRenderer PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${STB_PATH}
    ${METALCPP_HOME}
    ${SDL_HOME}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../3rdparty
)

target_compile_definitions(WallPaperRenderer PRIVATE USE_METAL)
target_compile_options(WallPaperRenderer PRIVATE -fobjc-arc)

target_link_libraries(WallPaperRenderer PRIVATE
    "${SDL_HOME}/build/libSDL3.dylib"
    "-framework Foundation"
    "-framework Metal"
    "-framework QuartzCore"
    "-framework CoreVideo"
    "-framework CoreMedia"
    "-framework VideoToolbox"
    "-framework AVFoundation"
    "-framework Cocoa"
    "-framework CoreGraphics"
)
