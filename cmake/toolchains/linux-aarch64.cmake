# Toolchain: Linux aarch64 cross-compiled from x86_64.
# Static linking so the tests can run under qemu-user without an ARM sysroot.
set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR aarch64)

set(CMAKE_C_COMPILER   aarch64-linux-gnu-gcc)
set(CMAKE_CXX_COMPILER aarch64-linux-gnu-g++)

set(CMAKE_FIND_ROOT_PATH /usr/aarch64-linux-gnu)
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)

set(CMAKE_EXE_LINKER_FLAGS_INIT "-static")

# Cross binaries run under user-mode emulation: good enough to prove they work,
# useless for timing. Speed measurements belong to the arm64 VM (docs/COMPAT.md).
set(CMAKE_CROSSCOMPILING_EMULATOR qemu-aarch64)
