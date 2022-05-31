// stdafx.h : include file for standard system include files,
// or project specific include files that are used frequently, but
// are changed infrequently
//

#pragma once

#ifdef _MSC_VER
#ifdef NDEBUG
#define AAssert(_Expression)     ((void)0)
#else
#define AAssert(_Expression) do {volatile int tmp = 0; (void)( (!!(_Expression)) || (tmp / tmp) );} while(false)
#endif
#endif

#include "targetver.h"

#define _CRT_SECURE_NO_WARNINGS

#include <stdio.h>
#include <tchar.h>



// TODO: reference additional headers your program requires here
