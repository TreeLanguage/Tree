cmake_minimum_required(VERSION 3.16)
project(tree VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(TREE_CONFIG_DIR "${CMAKE_BINARY_DIR}/generated")

include(GNUInstallDirs)

configure_file(
    "${CMAKE_SOURCE_DIR}/include/tree/config.hpp.in"
    "${TREE_CONFIG_DIR}/tree/config.hpp"
    @ONLY
)

enable_testing()

add_subdirectory(library/tree)
add_subdirectory(app)
add_subdirectory(test)
