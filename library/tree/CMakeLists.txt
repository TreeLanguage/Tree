file(GLOB TREE_SOURCES "src/*.cpp")
add_library(treelib STATIC ${TREE_SOURCES})

target_include_directories(treelib
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<BUILD_INTERFACE:${TREE_CONFIG_DIR}>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        src
)