// Compatibility definitions for the legacy codec sources on non-x86 targets.
//
// The 7-Zip AES code in _Encryption/7zAES only provides CPU_Is_Aes_Supported()
// for x86/x64 (its CpuArch.c is x86 assembly based). On arm64 the encryption
// code still works through the generic C path, it just cannot detect the AES
// instructions, so the probe always answers "no".
#include "_Encryption/7zAES/CpuArch.h"

#if !defined(__i386__) && !defined(__x86_64__) && !defined(_M_IX86) && !defined(_M_X64)

Bool CPU_Is_Aes_Supported()
{
    return False;
}

#endif
