cmake_minimum_required(VERSION 3.20)
project(WallPaper LANGUAGES CXX)

set(SDL_HOME $ENV{SDL_HOME})
set(STB_PATH $ENV{STB_PATH})
message(STATUS "SDL_HOME : ${SDL_HOME}")
message(STATUS "STB_PATH : ${STB_PATH}")

if(NOT EXISTS "${SDL_HOME}/build/libSDL3.dylib")
    message(FATAL_ERROR "SDL3 library not found: ${SDL_HOME}/build/libSDL3.dylib")
endif()

# ffmpeg
set(FFMPEG_HOME "" CACHE PATH "FFmpeg installation prefix")
if(NOT FFMPEG_HOME)
    if(DEFINED ENV{FFMPEG_HOME} AND EXISTS "$ENV{FFMPEG_HOME}/include/libavformat/avformat.h")
        set(FFMPEG_HOME "$ENV{FFMPEG_HOME}")
    elseif(APPLE AND EXISTS "/opt/homebrew/opt/ffmpeg/include/libavformat/avformat.h")
        set(FFMPEG_HOME "/opt/homebrew/opt/ffmpeg")
    endif()
endif()

if(FFMPEG_HOME)
    set(FFMPEG_INCLUDE_DIRS "${FFMPEG_HOME}/include")
    set(FFMPEG_LIBRARY_DIRS "${FFMPEG_HOME}/lib")
    set(FFMPEG_LIBRARIES avformat avcodec avutil swscale)
else()
    if(APPLE)
        execute_process(
            COMMAND brew --prefix
            OUTPUT_VARIABLE HOMEBREW_PREFIX
            OUTPUT_STRIP_TRAILING_WHITESPACE
        )
        set(ENV{PKG_CONFIG_PATH}
            "${HOMEBREW_PREFIX}/opt/ffmpeg/lib/pkgconfig:$ENV{PKG_CONFIG_PATH}"
        )
    endif()
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(FFMPEG REQUIRED
        libavformat
        libavcodec
        libavutil
        libswscale
    )
endif()

add_executable(WallPaper 
        uimain.cpp 
        childprocess.cpp 
        videodecode.cpp 
        uiutils.cpp
        loadconfig.cpp
        )

if(ENABLE_LOGGING)
    target_compile_definitions(WallPaper PRIVATE ENABLE_LOGGING)
endif()

# 设置BUNDLE后可直接以app形式运行
set_target_properties(WallPaper PROPERTIES
    MACOSX_BUNDLE TRUE
    MACOSX_BUNDLE_GUI_IDENTIFIER "com.yourname.WallPaper"
    MACOSX_BUNDLE_BUNDLE_NAME "WallPaper"
    MACOSX_BUNDLE_SHORT_VERSION_STRING "1.0"
    MACOSX_BUNDLE_BUNDLE_VERSION "1"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/build"
)

target_include_directories(WallPaper PRIVATE 
    "${SDL_HOME}/include" 
    ${STB_PATH}
    ${CMAKE_CURRENT_SOURCE_DIR}/../3rdparty
    )

target_link_libraries(WallPaper PRIVATE
    "${SDL_HOME}/build/libSDL3.dylib"
    "-Wl,-rpath,${SDL_HOME}/build"
)

target_include_directories(WallPaper PRIVATE ${FFMPEG_INCLUDE_DIRS})
target_link_directories(WallPaper PRIVATE ${FFMPEG_LIBRARY_DIRS})
target_link_libraries(WallPaper PRIVATE ${FFMPEG_LIBRARIES})

if(APPLE)
    target_compile_options(WallPaper PRIVATE ${FFMPEG_CFLAGS_OTHER})
    target_link_libraries(WallPaper PRIVATE
        "-framework CoreText"
        "-framework CoreGraphics"
        "-framework CoreFoundation"
    )
endif()

set_target_properties(WallPaper PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/build"
)
