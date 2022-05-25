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

#include "stdafx.h"

#include "utfconv.h"
#include <stdbool.h>
#include <assert.h>

// The type of a single Unicode codepoint
typedef uint32_t codepoint_t;

// The last codepoint of the Basic Multilingual Plane, which is the part of Unicode that
// UTF-16 can encode without surrogates
#define BMP_END 0xFFFF

// The highest valid Unicode codepoint
#define UNICODE_MAX 0x10FFFF

// The codepoint that is used to replace invalid encodings
#define INVALID_CODEPOINT 0xFFFD

// If a character, masked with GENERIC_SURROGATE_MASK, matches this value, it is a surrogate.
#define GENERIC_SURROGATE_VALUE 0xD800
// The mask to apply to a character before testing it against GENERIC_SURROGATE_VALUE
#define GENERIC_SURROGATE_MASK 0xF800

// If a character, masked with SURROGATE_MASK, matches this value, it is a high surrogate.
#define HIGH_SURROGATE_VALUE 0xD800
// If a character, masked with SURROGATE_MASK, matches this value, it is a low surrogate.
#define LOW_SURROGATE_VALUE 0xDC00
// The mask to apply to a character before testing it against HIGH_SURROGATE_VALUE or LOW_SURROGATE_VALUE
#define SURROGATE_MASK 0xFC00

// The value that is subtracted from a codepoint before encoding it in a surrogate pair
#define SURROGATE_CODEPOINT_OFFSET 0x10000
// A mask that can be applied to a surrogate to extract the codepoint value contained in it
#define SURROGATE_CODEPOINT_MASK 0x03FF
// The number of bits of SURROGATE_CODEPOINT_MASK
#define SURROGATE_CODEPOINT_BITS 10


// The highest codepoint that can be encoded with 1 byte in UTF-8
#define UTF8_1_MAX 0x7F
// The highest codepoint that can be encoded with 2 bytes in UTF-8
#define UTF8_2_MAX 0x7FF
// The highest codepoint that can be encoded with 3 bytes in UTF-8
#define UTF8_3_MAX 0xFFFF
// The highest codepoint that can be encoded with 4 bytes in UTF-8
#define UTF8_4_MAX 0x10FFFF

// If a character, masked with UTF8_CONTINUATION_MASK, matches this value, it is a UTF-8 continuation byte
#define UTF8_CONTINUATION_VALUE 0x80
// The mask to a apply to a character before testing it against UTF8_CONTINUATION_VALUE
#define UTF8_CONTINUATION_MASK 0xC0
// The number of bits of a codepoint that are contained in a UTF-8 continuation byte
#define UTF8_CONTINUATION_CODEPOINT_BITS 6

// Represents a UTF-8 bit pattern that can be set or verified
typedef struct
{
    // The mask that should be applied to the character before testing it
    utf8_t mask;
    // The value that the character should be tested against after applying the mask
    utf8_t value;
	size_t u16l;	// number of utf16 chars to hold this codepoint
} utf8_pattern;

// The patterns for leading bytes of a UTF-8 codepoint encoding
// Each pattern represents the leading byte for a character encoded with N UTF-8 bytes,
// where N is the index + 1
static const utf8_pattern utf8_leading_bytes[] =
{
    { 0x80, 0x00, 1 }, // 0xxxxxxx
    { 0xE0, 0xC0, 1 }, // 110xxxxx
    { 0xF0, 0xE0, 1 }, // 1110xxxx
    { 0xF8, 0xF0, 2 }  // 11110xxx
};

// The number of elements in utf8_leading_bytes
#define UTF8_LEADING_BYTES_LEN 4


///////////////////////////////////////
// utf16 to utf8

inline codepoint_t decode_utf16(const utf16_t *&utf16)
{
	if (*utf16 == 0)
		return 0;
	utf16_t high = *utf16++;

	// BMP character
	if ((high & GENERIC_SURROGATE_MASK) != GENERIC_SURROGATE_VALUE)
		return high;

	if ((high & SURROGATE_MASK) != HIGH_SURROGATE_VALUE || !utf16[1])
		return INVALID_CODEPOINT;	// Unmatched low surrogate or EOS, invalid
	utf16_t low = *utf16++;
	if ((low & SURROGATE_MASK) != LOW_SURROGATE_VALUE)
		return INVALID_CODEPOINT;	// Unmatched high surrogate, invalid
	codepoint_t result = (high & SURROGATE_CODEPOINT_MASK) << SURROGATE_CODEPOINT_BITS;
	result = (result | (low & SURROGATE_CODEPOINT_MASK)) + SURROGATE_CODEPOINT_OFFSET;
	return result;
}

inline size_t utf8_char_len(codepoint_t codepoint)
{
	if (codepoint <= UTF8_1_MAX)
		return 1;
	else if (codepoint <= UTF8_2_MAX)
		return 2;
	else if (codepoint <= UTF8_3_MAX)
		return 3;
	return 4;
}

inline size_t utf16to8_char_len(const utf16_t *&utf16)
{
	return utf8_char_len(decode_utf16(utf16));
}

inline size_t encode_utf8(codepoint_t codepoint, abuf<utf8_t> &u8, size_t index)
{
	int size = utf8_char_len(codepoint);
	assert(index + size < u8.size());

	// Write the continuation bytes in reverse order
	for (int cont_index = size - 1; cont_index > 0; cont_index--)
	{
		u8[index + cont_index] = (codepoint & ~UTF8_CONTINUATION_MASK) | UTF8_CONTINUATION_VALUE;
		codepoint >>= UTF8_CONTINUATION_CODEPOINT_BITS;
	}

	// Write the leading byte
	u8[index] = (codepoint & ~(utf8_leading_bytes[size - 1].mask)) | utf8_leading_bytes[size - 1].value;

	return size;
}

size_t utf16to8_len(const utf16_t *u16)
{
	size_t u8len = 0;
	for (const utf16_t *u16t = u16; *u16t;)
		u8len += utf16to8_char_len(u16t);
	return u8len;
}

size_t utf16to8(const utf16_t *u16, abuf<utf8_t> &u8)
{
	size_t u8len = 0;
	// determine the output size
	for (const utf16_t *u16t = u16; *u16t;)
		u8len += utf16to8_char_len(u16t);
	// conv
	u8.resize(u8len + 1);
	u8len = 0;
	for (const utf16_t *u16t = u16; *u16t;)
		u8len += encode_utf8(decode_utf16(u16t), u8, u8len);
	u8[u8len] = 0;
	return u8len;
}

size_t utf16to8(const utf16_t *u16, abuf<utf8_t> &u8, size_t &index)
{
	for (const utf16_t *u16t = u16; *u16t;)
		index += encode_utf8(decode_utf16(u16t), u8, index);
	u8[index] = 0;
	return index;
}


///////////////////////////////////////
// utf8 to utf16

// return the number of utf16 chars to hold current utf8 char and move utf8 to the next char
inline size_t utf8to16_char_len(const utf8_t *&utf8)
{
	if (!*utf8)
		return 0;
	for (size_t i = 0; i < sizeof(utf8_leading_bytes) / sizeof(utf8_leading_bytes[0]); ++i)
	{
		if ((*utf8 & utf8_leading_bytes[i].mask) == utf8_leading_bytes[i].value)
		{
			for (size_t j = 0; j < i; ++j)
			{
				if (!*++utf8)	// invalid null byte in the middle
					return 1;
			}
			utf8 += 1;
			return utf8_leading_bytes[i].u16l;
		}
	}
	// invalid byte
	++utf8;
	return 1;
}

inline size_t encode_utf16(codepoint_t codepoint, abuf<utf16_t> &u16, size_t index)
{
	if (codepoint == 0)
		return 0;

	assert(index < u16.size() - 1);
	if (codepoint <= BMP_END)
	{
		u16[index] = codepoint;
		return 1;
	}

	assert(index < u16.size() - 2);
	codepoint -= SURROGATE_CODEPOINT_OFFSET;
	utf16_t low = LOW_SURROGATE_VALUE | (codepoint & SURROGATE_CODEPOINT_MASK);
	codepoint >>= SURROGATE_CODEPOINT_BITS;
	utf16_t high = HIGH_SURROGATE_VALUE | (codepoint & SURROGATE_CODEPOINT_MASK);
	u16[index] = high;
	u16[index + 1] = low;

	return 2;
}

inline codepoint_t decode_utf8(const utf8_t *&utf8)
{
	if (!*utf8)
		return 0;

	// determine the number of bytes in this uchar
	size_t u8len = 0;
	for (size_t i = 0; i < UTF8_LEADING_BYTES_LEN; ++i)
	{
		if ((*utf8 & utf8_leading_bytes[i].mask) == utf8_leading_bytes[i].value)
		{
			u8len = (i + 1);
			break;
		}
	}
	if (u8len == 0)
	{
		utf8 += 1;
		return INVALID_CODEPOINT;
	}

	// decode
	codepoint_t codepoint = *utf8++ & ~utf8_leading_bytes[u8len - 1].mask;
	for (size_t i = 1; i < u8len; ++i, ++utf8)
	{
		if (*utf8 == 0)
			return INVALID_CODEPOINT;
		codepoint = (codepoint << UTF8_CONTINUATION_CODEPOINT_BITS) | (*utf8 & ~UTF8_CONTINUATION_MASK);
	}

	// Surrogates are invalid Unicode codepoints, and should only be used in UTF-16
	if (codepoint < BMP_END && (codepoint & GENERIC_SURROGATE_MASK) == GENERIC_SURROGATE_VALUE)
		return INVALID_CODEPOINT;
	// UTF-8 can encode codepoints larger than the Unicode standard allows
	if (codepoint > UNICODE_MAX)
		return INVALID_CODEPOINT;

	return codepoint;
}

size_t utf8to16_len(const utf8_t *u8)
{
	size_t utf16_len = 0;
	// determine the output size
	for (const utf8_t *u8t = u8; *u8t;)
		utf16_len += utf8to16_char_len(u8t);
	return utf16_len;
}

size_t utf8to16(const utf8_t *u8, abuf<utf16_t> &u16)
{
	size_t utf16_index = 0;
	// determine the output size
	for (const utf8_t *u8t = u8; *u8t; )
		utf16_index += utf8to16_char_len(u8t);
	// conv
	u16.resize(utf16_index + 1);
	utf16_index = 0;
	for (const utf8_t *u8t = u8; *u8t;)
		utf16_index += encode_utf16(decode_utf8(u8t), u16, utf16_index);
	u16[utf16_index] = 0;
	return utf16_index;
}

size_t utf8to16(const utf8_t *u8, abuf<utf16_t> &u16, size_t &index)
{
	for (const utf8_t *u8t = u8; *u8t;)
		index += encode_utf16(decode_utf8(u8t), u16, index);
	u16[index] = 0;
	return index;
}
