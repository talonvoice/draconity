cmake_minimum_required(VERSION 3.10.0)

project(draconity)
set(CMAKE_MACOSX_RPATH 1)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

set(CMAKE_INSTALL_RPATH_USE_LINK_PATH FALSE)
set(CMAKE_SKIP_BUILD_RPATH TRUE)
set(CMAKE_BUILD_WITH_INSTALL_RPATH FALSE)

link_directories(${CMAKE_LIBRARY_OUTPUT_DIRECTORY})
include_directories(/usr/include/libbson-1.0 /usr/local/include/libbson-1.0)

include_directories(src)
if (ASAN)
    add_definitions(-fsanitize=address)
endif()
if (DEBUG)
    add_definitions(-O0 -ggdb -fno-omit-frame-pointer)
else()
    add_definitions(-O2)
endif()

find_library(BSON NAMES libbson-static-1.0.a)
if (NOT BSON)
    # Got an error here and are on linux? the apt package doesn't have a static
    # lib for libbson, so need to build it yourself and then point cmake at it.
    # Do an in-source build for libbson using the ccmake UI to turn on static build.
    # You also need to add -fPIC to cmake's CFLAGS.
    # Then invoke cmake for Draconity like:
    # env CMAKE_LIBRARY_PATH=/home/caspar/src/libbson-1.9.2 cmake .
    # Probably there's a better way but hey this works for now :)
    message(FATAL_ERROR "libbson not found")
endif()
find_library(ZYDIS NAMES libzydis.a zydis Zydis)
if (NOT ZYDIS)
    message(FATAL_ERROR "libzydis not found")
endif()
find_library(UV NAMES uv)
if (NOT UV)
    message(FATAL_ERROR, "libuv not found")
endif()
include_directories(vendor/uvw/src)

file(GLOB_RECURSE SOURCE src/*.c src/*.cpp)
add_library(draconity SHARED ${SOURCE})
target_link_libraries(draconity m dl pthread ${UV} ${ZYDIS} ${BSON})
if(APPLE)
    target_link_libraries(draconity -F/System/Library/PrivateFrameworks "-framework CoreSymbolication -framework AppKit")
endif()
if(MINGW)
    target_link_libraries(draconity ws2_32 psapi iphlpapi userenv)
endif()

set_target_properties(draconity PROPERTIES
    CXX_STANDARD 17
    CXX_EXTENSIONS OFF
)
