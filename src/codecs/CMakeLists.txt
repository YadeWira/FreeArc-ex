add_library(fa_codecs STATIC
    LZMA2/C/LzmaDec.c
)

target_include_directories(fa_codecs PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
add_library(fa::codecs ALIAS fa_codecs)

# Legacy vendor code: it is 2009-era C and is compiled with the warnings the
# upstream build used. New code (src/core, src/cli) keeps the project's warnings.
target_compile_options(fa_codecs PRIVATE -w)
set_target_properties(fa_codecs PROPERTIES C_STANDARD 99 C_STANDARD_REQUIRED ON)
