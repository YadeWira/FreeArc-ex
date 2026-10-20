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

# Chain methods (rep+4x4+tor) decode through the legacy engine.
add_test(NAME chain_decode COMMAND arc-ex t ${CMAKE_CURRENT_SOURCE_DIR}/fixtures/oracle-m1.arc)

# Creation round trip (no external tools needed) for the methods M1 supports.
foreach(method 0 lzma rep+lzma 4x4+lzma ppmd:8:32mb)
    add_test(NAME create_${method}
             COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/integration/create.sh
                     $<TARGET_FILE:arc-ex> ${method})
endforeach()

# Differential test against the original binary, when wine and the oracle are
# available on the build machine (docs/COMPAT.md).
find_program(FA_WINE NAMES wine)
set(FA_ARC_ORACLE_DEFAULT "/mnt/IA_LAB/agentes/arc/bin/Arc.exe")
if(FA_WINE AND EXISTS "${FA_ARC_ORACLE_DEFAULT}")
    foreach(method 0 lzma rep+lzma 4x4+lzma ppmd:8:32mb)
        add_test(NAME oracle_reads_ours_${method}
                 COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/integration/oracle.sh
                         $<TARGET_FILE:arc-ex> ${FA_ARC_ORACLE_DEFAULT}
                         ${FA_WINE} ${method})
    endforeach()
endif()

add_executable(preset_test unit/preset_test.cpp)
target_link_libraries(preset_test PRIVATE fa::core)
add_test(NAME preset COMMAND preset_test)
set_tests_properties(preset PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

# PPMd: 32-bit layout emulated on 64-bit (low heap + 32-bit references).
add_test(NAME ppmd_decode COMMAND arc-ex t ${CMAKE_CURRENT_SOURCE_DIR}/fixtures/oracle-ppmd.arc)
add_test(NAME create_ppmd
         COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/integration/create.sh
                 $<TARGET_FILE:arc-ex> ppmd:8:32mb)

# LZ4 is still pending its 1.10 update: creation must fail cleanly.
add_test(NAME lz4_rejected
         COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/integration/expect_unsupported.sh
                 $<TARGET_FILE:arc-ex> lz4)

foreach(method lzma rep+lzma)
    add_test(NAME update_${method}
             COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/integration/update.sh
                     $<TARGET_FILE:arc-ex> ${method})
endforeach()

find_program(FA_WINE_SOLID NAMES wine)
set(FA_ARC_ORACLE_SOLID "/mnt/IA_LAB/agentes/arc/bin/Arc.exe")
if(FA_WINE_SOLID AND EXISTS "${FA_ARC_ORACLE_SOLID}")
    add_test(NAME solid_blocks
             COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/integration/solid.sh
                     $<TARGET_FILE:arc-ex> ${FA_ARC_ORACLE_SOLID} ${FA_WINE_SOLID})
else()
    add_test(NAME solid_blocks
             COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/integration/solid.sh
                     $<TARGET_FILE:arc-ex>)
endif()

add_test(NAME block_reuse
         COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/integration/reuse.sh $<TARGET_FILE:arc-ex>)

if(FA_WINE_SOLID AND EXISTS "${FA_ARC_ORACLE_SOLID}")
    add_test(NAME crypt
             COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/integration/crypt.sh
                     $<TARGET_FILE:arc-ex> ${FA_ARC_ORACLE_SOLID} ${FA_WINE_SOLID})
else()
    add_test(NAME crypt
             COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/integration/crypt.sh $<TARGET_FILE:arc-ex>)
endif()

if(FA_WINE_SOLID AND EXISTS "${FA_ARC_ORACLE_SOLID}")
    add_test(NAME recovery
             COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/integration/recovery.sh
                     $<TARGET_FILE:arc-ex> ${FA_ARC_ORACLE_SOLID} ${FA_WINE_SOLID})
else()
    add_test(NAME recovery
             COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/integration/recovery.sh $<TARGET_FILE:arc-ex>)
endif()

add_test(NAME cli_features
         COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/integration/cli.sh $<TARGET_FILE:arc-ex>)

add_test(NAME lua_events
         COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/integration/lua.sh $<TARGET_FILE:arc-ex>)
