/* Case-folding bridge for cross builds.
 *
 * The 2009 sources include <ShObjIdl.h> and <Wincrypt.h> with capitalisation
 * that only works on case-insensitive file systems. The MinGW headers shipped
 * with the cross toolchain are lower case, so these shims forward to the real
 * ones when compiling on Linux for Windows. */
#pragma once
#include <shobjidl.h>
