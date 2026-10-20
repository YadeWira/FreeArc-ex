add_library(fa_core STATIC
    src/crc32.cpp
    src/vint.cpp
    src/arc.cpp
    src/method.cpp
    src/decompress.cpp
)

target_include_directories(fa_core PUBLIC include)
target_link_libraries(fa_core PRIVATE fa::codecs)
add_library(fa::core ALIAS fa_core)
