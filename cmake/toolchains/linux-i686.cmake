# Toolchain: native Linux i686 (32-bit) using gcc multilib.
set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR i686)

set(CMAKE_C_FLAGS_INIT   "-m32")
set(CMAKE_CXX_FLAGS_INIT "-m32")
set(CMAKE_EXE_LINKER_FLAGS_INIT    "-m32")
set(CMAKE_SHARED_LINKER_FLAGS_INIT "-m32")

# i686 without mandatory SSE2: FreeArc 0.67 ran on an i486.
set(CMAKE_C_FLAGS_INIT   "${CMAKE_C_FLAGS_INIT} -march=i486 -mtune=pentiumpro")
set(CMAKE_CXX_FLAGS_INIT "${CMAKE_CXX_FLAGS_INIT} -march=i486 -mtune=pentiumpro")
