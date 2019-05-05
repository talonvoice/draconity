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
include_directories(/usr/local/include/libbson-1.0)
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
find_library(ZYDIS NAMES libzydis.a zydis)
if (NOT BSON)
    message(FATAL_ERROR "libbson not found")
endif()
if (NOT ZYDIS)
    message(FATAL_ERROR "libzydis not found")
endif()

file(GLOB_RECURSE SOURCE src/*.c src/*.m src/*.cpp)
add_library(draconity SHARED ${SOURCE})
target_link_libraries(draconity m dl pthread ${BSON} ${ZYDIS})
target_link_libraries(draconity -F/System/Library/PrivateFrameworks "-framework CoreSymbolication -framework AppKit")
set_target_properties(draconity PROPERTIES
    CXX_STANDARD 17
    CXX_EXTENSIONS OFF
)
