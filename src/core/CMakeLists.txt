add_library(fa_core STATIC
    src/crc32.cpp
    src/vint.cpp
    src/arc.cpp
    src/mapped_file.cpp
    src/method.cpp
    src/decompress.cpp
    src/codec.cpp
    src/arc_writer.cpp
    src/preset.cpp
)

target_include_directories(fa_core PUBLIC include)
# WHOLE_ARCHIVE: every codec must be linked in so its static initializer runs.
target_link_libraries(fa_core PRIVATE $<LINK_LIBRARY:WHOLE_ARCHIVE,fa::legacy>)
add_library(fa::core ALIAS fa_core)
