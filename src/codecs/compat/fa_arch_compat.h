// Force-included into every legacy codec translation unit.
//
// On non-x86 targets the 7-Zip AES helper CPU_Is_Aes_Supported() is not declared
// (its header only declares it under MY_CPU_X86_OR_AMD64), but C_Encryption.cpp
// calls it unconditionally. Declared here, defined in fa_compat.cpp.
#if !defined(__i386__) && !defined(__x86_64__) && !defined(_M_IX86) && !defined(_M_X64)
extern "C" {
int CPU_Is_Aes_Supported();
}
#endif
