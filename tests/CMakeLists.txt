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

# Fixture produced by Arc.exe 0.67.1 (`a -m1 -dm0`): proves the reader works on a
# real archive without needing wine at test time.
add_test(NAME fixture_list
         COMMAND arc-ex l ${CMAKE_CURRENT_SOURCE_DIR}/fixtures/oracle-m1.arc)
set_tests_properties(fixture_list PROPERTIES
    PASS_REGULAR_EXPRESSION "4 file\\(s\\), 28000 byte\\(s\\)")

# Fixtures produced by Arc.exe 0.67.1 with the methods M1 supports. `t` verifies
# every per-file CRC; `x` output is compared against the source tree hashes.
foreach(fixture m0 lzma lzmadir)
    add_test(NAME test_${fixture} COMMAND arc-ex t ${CMAKE_CURRENT_SOURCE_DIR}/fixtures/${fixture}.arc)
    add_test(NAME extract_${fixture}
             COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/integration/extract.sh
                     $<TARGET_FILE:arc-ex>
                     ${CMAKE_CURRENT_SOURCE_DIR}/fixtures/${fixture}.arc
                     ${CMAKE_CURRENT_SOURCE_DIR}/fixtures/src.sha256)
endforeach()

# Archives whose data uses a method chain must fail loudly, not silently.
add_test(NAME chain_rejected COMMAND arc-ex t ${CMAKE_CURRENT_SOURCE_DIR}/fixtures/oracle-m1.arc)
set_tests_properties(chain_rejected PROPERTIES WILL_FAIL TRUE)
