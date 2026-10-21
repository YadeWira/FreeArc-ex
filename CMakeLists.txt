cmake_minimum_required(VERSION 3.25)

project(FreeArcEx
    VERSION 0.1.0
    DESCRIPTION "FreeArc-ex: extended support for FreeArc 0.67"
    LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_C_STANDARD 11)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(FA_BUILD_CLI   "Build the arc-ex CLI"                 ON)
option(FA_BUILD_GUI   "Build the GTK3 GUI (gtkmm 3.24)"      OFF)
option(FA_BUILD_TESTS "Build and register the tests"         ON)
option(FA_WERROR      "Treat warnings as errors"             OFF)

if(MSVC)
    add_compile_options(/W4 /permissive-)
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

if(FA_WERROR)
    add_compile_options(-Werror)
endif()

# Windows 7 is the minimum supported version (see docs/COMPAT.md).
if(MINGW)
    add_compile_definitions(_WIN32_WINNT=0x0601 WINVER=0x0601)
endif()

add_subdirectory(src/lua)
add_subdirectory(src/codecs)
add_subdirectory(src/core)

if(FA_BUILD_CLI)
    add_subdirectory(src/cli)
endif()

if(FA_BUILD_GUI)
    add_subdirectory(src/gui)
endif()

if(FA_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
