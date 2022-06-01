// A modified version of: https://github.com/Davipb/utf8-utf16-converter
//
// Copyright 2019 Davipb
// 
// Permission is hereby granted, free of charge, to any person obtaining a copy of this
// software and associated documentation files(the "Software"), to deal in the Software
// without restriction, including without limitation the rights to use, copy, modify,
// merge, publish, distribute, sublicense, and / or sell copies of the Software, and to
// permit persons to whom the Software is furnished to do so, subject to the following
// conditions :
// 
// The above copyright notice and this permission notice shall be included in all copies
// or substantial portions of the Software.
// 
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
// INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A
// PARTICULAR PURPOSE AND NONINFRINGEMENT.IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
// HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF
// CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE
// OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

#pragma once
#include <stdint.h>
#include <stddef.h>
#include "auto_buf.hpp"

typedef uint8_t utf8_t; // The type of a single UTF-8 character
#ifdef _WIN32
static_assert(sizeof(wchar_t) == 2, "Invalid wchar_t size");
typedef wchar_t utf16_t;
#else
typedef uint16_t utf16_t; // The type of a single UTF-16 character
#endif

size_t utf16to8_len(const utf16_t *u16);
size_t utf16to8(const utf16_t *u16, abuf<utf8_t> &u8);
size_t utf16to8(const utf16_t *u16, abuf<utf8_t> &u8, size_t &start);
size_t utf8to16_len(const utf8_t *u8);
size_t utf8to16_len(const utf8_t *u8, size_t len);
size_t utf8to16(const utf8_t *u8, abuf<utf16_t> &u16);
size_t utf8to16(const utf8_t *u8, abuf<utf16_t> &u16, size_t &start);
size_t utf8to16(const utf8_t *u8, size_t len, abuf<utf16_t> &u16, size_t &start);

inline size_t utf16to8(const utf16_t *u16, abuf<char> &u8) { return utf16to8(u16, *(abuf<utf8_t>*)&u8); }
inline size_t utf8to16_len(const char *u8) { return utf8to16_len((const utf8_t *)u8); }
inline size_t utf8to16_len(const char *u8, size_t len) { return utf8to16_len((const utf8_t *)u8, len); }
inline size_t utf8to16(const char *u8, abuf<utf16_t> &u16) { return utf8to16((const utf8_t *)u8, u16); }
inline size_t utf8to16(const char *u8, abuf<utf16_t> &u16, size_t &start) { return utf8to16((const utf8_t *)u8, u16, start); }
