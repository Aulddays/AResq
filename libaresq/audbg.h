#pragma once

#include "pe_log.h"

#ifdef _WIN32

#define NOMINMAX 1
#include <Windows.h>
#include <crtdbg.h>

#ifndef _DEBUG
#define _CrtDbgReport(_Expression)     ((void)0)
#endif

#define AuVerify(_Expression) do { \
	if (!(_Expression)) { \
		PELOG_LOG((PLV_ERROR, "AuVerify Failed. %s:%d %s\n", __FILE__, __LINE__, __FUNCTION__)); \
		pelog_flush(); \
		_CrtDbgReport(_CRT_ASSERT, __FILE__, __LINE__, NULL, NULL); \
		DebugBreak();} \
} while(0)

#ifdef NDEBUG
#define AuAssert(_Expression)     ((void)0)
#else	// not NDEBUG
#define AuAssert(_Expression)     AuVerify(_Expression)
#endif	// NDEBUG


#else	// not _WIN32

#include <assert.h>
#define AuAssert(_Expression) assert(_Expression)
#define AuVerify(_Expression) do { if (!(_Expression)) assert(false); } while (0)

#endif

