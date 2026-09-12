cmake_minimum_required(VERSION 3.16)
project(tree VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(TREE_CONFIG_DIR "${CMAKE_BINARY_DIR}/generated")

include(GNUInstallDirs)
include(FetchContent)

if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    option(ENABLE_ASAN "Build with Sanitizer" ON)
else()
    option(ENABLE_ASAN "Build with Sanitizer" OFF)
endif()

if(ENABLE_ASAN)
    add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer -g)
    add_link_options(-fsanitize=address,undefined)
endif()

configure_file(
    "${CMAKE_SOURCE_DIR}/include/tree/config.h.in"
    "${TREE_CONFIG_DIR}/tree/config.h"
    @ONLY
)

enable_testing()

add_subdirectory(library/tree)
add_subdirectory(app)
add_subdirectory(test)
