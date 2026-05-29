// stdafx.h : include file for standard system include files,
// or project specific include files that are used frequently, but
// are changed infrequently
//

#pragma once

#if !defined(__linux__) && !defined(_WIN32)
#	error Unsupported platform
#endif

#ifdef _WIN32

#define NOMINMAX 1

#include "targetver.h"

#define _CRT_SECURE_NO_WARNINGS
#define WIN32_LEAN_AND_MEAN             // Exclude rarely-used stuff from Windows headers
#define __STDC_FORMAT_MACROS          // for printf format macros in inttypes.h

#endif	// _WIN32

#ifdef __MINGW32__
#	define __USE_MINGW_ANSI_STDIO 1
#	define __MINGW_USE_VC2005_COMPAT
#endif

#ifdef __GLIBC__
#	define _TIME_BITS 64
#endif

#include <stdio.h>

// TODO: reference additional headers your program requires here
