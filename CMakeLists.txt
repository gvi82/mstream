cmake_minimum_required(VERSION 2.5)

if (CMAKE_BUILD_TYPE MATCHES "Debug")
    set(ENV{PKG_CONFIG_PATH}  "/home/gvi/ffmpegd_build/lib/pkgconfig")
else (CMAKE_BUILD_TYPE MATCHES "Debug")
    set(ENV{PKG_CONFIG_PATH}  "/home/gvi/ffmpeg_build/lib/pkgconfig")
endif(CMAKE_BUILD_TYPE MATCHES "Debug")

set(CMAKE_CXX_STANDARD 11)

add_executable( stream src/stream.cpp
    src/decoder.cpp
    src/encoder.cpp
    src/common.cpp
    )

INCLUDE(FindPkgConfig)
pkg_check_modules(SDL REQUIRED sdl)

find_package(PkgConfig REQUIRED)
pkg_check_modules(FFMPEG  REQUIRED  libavdevice libavformat libavfilter libavcodec libavutil libswscale)

include_directories(player PUBLIC ${FFMPEG_INCLUDE_DIRS})
include_directories(include)
link_directories(${FFMPEG_LIBRARY_DIRS})

target_link_libraries( stream
                       PRIVATE ${FFMPEG_LDFLAGS} m ${SDL_LDFLAGS} ${CMAKE_DL_LIBS} ${PLATFORM_SPECIFIC_LIBS} )
