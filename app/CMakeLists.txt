add_executable(tree main.cpp)

target_link_libraries(tree PRIVATE treelib)

install(TARGETS tree
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})