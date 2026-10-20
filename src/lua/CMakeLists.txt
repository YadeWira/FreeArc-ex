# Lua 5.1, embedded for the arc.ini event scripts (same version the original
# used; 5.4 breaks the 5.1-era scripts).
file(GLOB FA_LUA_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/*.c")
add_library(fa_lua STATIC ${FA_LUA_SOURCES})
target_include_directories(fa_lua PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_options(fa_lua PRIVATE -w)
set_target_properties(fa_lua PROPERTIES C_STANDARD 99)
if(NOT WIN32)
    target_compile_definitions(fa_lua PRIVATE LUA_USE_LINUX)
endif()
