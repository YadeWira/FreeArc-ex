# Legacy compression library (FreeArc 0.67), used as the codec engine.
#
# These are the original sources, unmodified: Common.cpp, CompressionLibrary.cpp
# and one translation unit per codec. The other .cpp files in the subdirectories
# are included by their C_*.cpp, exactly as the upstream makefiles do.
# A static archive linked whole (see src/core/CMakeLists.txt): each codec
# registers itself through a static initializer, and a plain archive would drop
# the members nobody refers to by symbol (the registry looks codecs up by name).
add_library(fa_legacy STATIC
    Common.cpp
    fa_compat.cpp
    CompressionLibrary.cpp
    4x4/C_4x4.cpp
    Delta/C_Delta.cpp
    Dict/C_Dict.cpp
    DisPack/C_DisPack.cpp
    _Encryption/C_Encryption.cpp
    External/C_External.cpp
    GRZip/C_GRZip.cpp
    LZ4/C_LZ4.cpp
    LZMA2/C_LZMA.cpp
    LZMA2/C_BCJ.cpp
    LZP/C_LZP.cpp
    MM/C_MM.cpp
    MM/C_TTA.cpp
    PPMD/C_PPMD.cpp
    REP/C_REP.cpp
    Tornado/C_Tornado.cpp
)

add_library(fa::legacy ALIAS fa_legacy)

target_include_directories(fa_legacy PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/_TABI
    ${CMAKE_CURRENT_SOURCE_DIR}/_Encryption/headers
    ${CMAKE_CURRENT_SOURCE_DIR}/compat
)

if(WIN32)
    target_compile_definitions(fa_legacy PRIVATE FREEARC_WIN FREEARC_INTEL_BYTE_ORDER _UNICODE UNICODE)
else()
    target_compile_definitions(fa_legacy PRIVATE
        FREEARC_UNIX FREEARC_INTEL_BYTE_ORDER _FILE_OFFSET_BITS=64 _LARGEFILE_SOURCE)
endif()

# Legacy vendor code: keep the project's warnings out of it.
target_compile_options(fa_legacy PRIVATE -w -Wno-narrowing -fno-rtti
                       -include "${CMAKE_CURRENT_SOURCE_DIR}/compat/fa_arch_compat.h")

# 64-bit pointer-aware code paths (Tornado match finder) are selected by this.
# With 64 bits the multithreaded match finders are disabled, so the threading
# helpers that C_LZMA.cpp used to pull in have to be compiled on their own.
if(CMAKE_SIZEOF_VOID_P EQUAL 8)
    target_compile_definitions(fa_legacy PRIVATE FREEARC_64BIT)
    target_sources(fa_legacy PRIVATE MultiThreading.cpp)
endif()
# The 2009 sources are C++03/11-era; building them as C++20 changes behaviour in
# at least the chain decoder (verified: the same block decodes at C++11 but not
# at C++20), so the standard is pinned for this target only.
set_target_properties(fa_legacy PROPERTIES CXX_STANDARD 11 CXX_STANDARD_REQUIRED ON)

find_package(Threads REQUIRED)
target_link_libraries(fa_legacy PUBLIC Threads::Threads)
