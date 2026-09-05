file(GLOB_RECURSE TEST_SOURCES "*.cpp")
add_executable(tests ${TEST_SOURCES})

target_link_libraries(tests PRIVATE treelib)

target_include_directories(tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_features(tests PRIVATE cxx_std_20)

add_test(NAME tests COMMAND tests)