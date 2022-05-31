// stdafx.h : include file for standard system include files,
// or project specific include files that are used frequently, but
// are changed infrequently
//

#pragma once

#if !defined(__linux__) && !defined(_WIN32)
#	error Unsupported platform
#endif


#include "targetver.h"

#define _CRT_SECURE_NO_WARNINGS
#define WIN32_LEAN_AND_MEAN             // Exclude rarely-used stuff from Windows headers

#ifdef _MSC_VER
#define NOMINMAX 1
#include <windows.h>
#include <assert.h>
#ifdef NDEBUG
#define AAssert(_Expression)     ((void)0)
#else
#define AAssert(_Expression) do {if(!(_Expression)){DebugBreak(); assert(false);}} while(false)
#endif
#else
#define AAssert(_Expression) assert(_Expression)
#endif


// TODO: reference additional headers your program requires here
