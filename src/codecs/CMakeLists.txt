add_library(fa_codecs STATIC
    LZMA2/C/LzmaDec.c
    LZMA2/C/LzmaEnc.c
    LZMA2/C/LzFind.c
)

target_include_directories(fa_codecs PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
add_library(fa::codecs ALIAS fa_codecs)

# Legacy vendor code: it is 2009-era C and is compiled with the warnings the
# upstream build used. New code (src/core, src/cli) keeps the project's warnings.
target_compile_options(fa_codecs PRIVATE -w -include "${CMAKE_CURRENT_SOURCE_DIR}/lzma_shim.h")

# The encoder and match finder were patched by FreeArc with C++ syntax
# (`size_t(2)` casts) and are compiled as C++ upstream too; the decoder stays C.
set_source_files_properties(LZMA2/C/LzmaEnc.c LZMA2/C/LzFind.c PROPERTIES LANGUAGE CXX)
set_target_properties(fa_codecs PROPERTIES C_STANDARD 99 C_STANDARD_REQUIRED ON)
