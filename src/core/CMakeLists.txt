add_library(fa_core STATIC
    src/crc32.cpp
)

target_include_directories(fa_core PUBLIC include)
add_library(fa::core ALIAS fa_core)
