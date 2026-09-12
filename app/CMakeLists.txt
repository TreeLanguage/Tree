FetchContent_Declare(
    cli
    GIT_REPOSITORY https://github.com/CLIUtils/CLI11.git
    GIT_TAG v2.7.2
)

FetchContent_MakeAvailable(cli)

add_executable(tree main.cpp cli.cpp)

target_include_directories(tree
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${TREE_CONFIG_DIR}
)

target_link_libraries(tree PRIVATE treelib CLI11::CLI11)

install(TARGETS tree
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
