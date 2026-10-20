add_executable(crc32_test unit/crc32_test.cpp)
target_link_libraries(crc32_test PRIVATE fa::core)
add_test(NAME crc32 COMMAND crc32_test)

add_executable(format_test unit/format_test.cpp)
target_link_libraries(format_test PRIVATE fa::core)
add_test(NAME format COMMAND format_test)

if(TARGET arc-ex)
    add_test(NAME cli_version COMMAND arc-ex --version)
    set_tests_properties(cli_version PROPERTIES
        PASS_REGULAR_EXPRESSION "FreeArc-ex 0\\.1\\.0 \\(compatible with FreeArc 0\\.67\\.1\\)")
endif()

add_executable(vint_test unit/vint_test.cpp)
target_link_libraries(vint_test PRIVATE fa::core)
add_test(NAME vint COMMAND vint_test)
