/*
auto bufer. Scenarios: 
1. 
void f() 
{ 
    char buf[1000];	// May cause stack overflow
    .....
} 
2. 
void g() 
{ 
    int buf = new int[1000];	// delete required
    ....
    delete[] buf;
} 
3. 
void h() 
{ 
	std::vector<char> buf(1000);	// constructor overhead
} 
 
auto buffer Usage:
#include <auto_buf.hpp> 
void f() 
{ 
    abuf<int> buf1(1000);	// Mostly exchangeable with int [1000]
    abuf<char> buf2(1000, true); 	// `true`: zero filled
    assert(buf2[0] == 0);
 
    abuf<char> buf3;	// buf3.isNull() == true
    buf3.resize(100);	// resizeable, but INVALIDATES formally obtained raw pointer
    strcpy(buf3, "test");	// exchangeable with `char*`
    char *pbuf3 = buf3;
    fprintf("%s\n", pbuf3);	// OK
    buf3[0] = 'b';
    buf3.resize(200);
    fprintf("%s\n", pbuf3);	// May CRASH. pbuf3 is INVALID after buf3.resize() !!!
    assert(strcmp(buf3, "best") == 0);	// retain content after resize
 
    // specialized for char*
    abufchar string1("test string");
    abufchar string1 = "test string";   // Acceptable, but not efficient as the above
 
    // shollow copy
    buf2.scopyFrom(string1);
 
    // Explicit release is not required. But `buf3.resize(0)` can do the trick if necessary
} 
 
*/

/*
To debug memory issues, enable the following
#define _ABUF_DEBUG 1
*/

#pragma once

#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <assert.h>

#ifdef _DEBUG
#	define _ABUF_DEBUG 1
#endif

#if defined(_ABUF_DEBUG) && !defined(_ABUF_DEBUG_PAD_)
#	define _ABUF_DEBUG_PAD_ 1
#endif

template <typename T>
	class abuf
{
protected:
	T *_buf;
#ifdef _ABUF_DEBUG_PAD_
	uint8_t *_inbuf;
#endif
	size_t _size;
protected:
	abuf(const T *);
	abuf &operator =(const T *);

	// in debug mode, append 4 extra bytes (0xfdfdfdfdu) to both beginning and end of _buf
	// and check the values frequently
	// if only _ABUF_DEBUG_PAD_ defined but not _ABUF_DEBUG, just do the padding but not checking
#ifdef _ABUF_DEBUG_PAD_
#	define abuf_mem_check_dword 0xfdfdfdfdu
#endif
#ifdef _ABUF_DEBUG
	// Failure of this assertion indicates there was an overflow while using abuf. Check your code!
#	define abuf_mem_check() do { \
	if(_size > 0) \
		AAssert( (*(uint32_t*)_inbuf) == abuf_mem_check_dword && \
			(*(uint32_t*)(_inbuf + 4 + _size * sizeof(T))) ==  abuf_mem_check_dword); \
	} while (false)
#	define abufchar_mem_check() do { \
	if(_size > 0) \
		AAssert( (*(uint32_t*)_inbuf) == abuf_mem_check_dword && \
			(*(uint32_t*)(_inbuf + 4 + _size * sizeof(char))) ==  abuf_mem_check_dword); \
	} while (false)
#else
#	define abuf_mem_check() ((void)0)
#	define abufchar_mem_check() ((void)0)
#endif

public:
	abuf():_buf(NULL), _size(0)
#ifdef _ABUF_DEBUG_PAD_
		,_inbuf(NULL)
#endif
	{}

	/**
	 * @param element_count number of elements wanted
	 * @param clear whether fill all elements with 0
	 */
	abuf(size_t element_count, bool clear = false)
	{
		if(clear)
		{
#ifdef _ABUF_DEBUG_PAD_
			_inbuf = (uint8_t *)calloc(element_count * sizeof(T) + 8, 1);	// calloc automatically fills _buf with 0
			if(_inbuf)
			{
				*(uint32_t*)_inbuf = abuf_mem_check_dword;
				*(uint32_t*)(_inbuf + element_count * sizeof(T) + 4) = abuf_mem_check_dword;
				_buf = (T *)(_inbuf + 4);
			}
			else
				_buf = NULL;
#else
			_buf = (T *)calloc(element_count, sizeof(T));	// calloc automatically fills _buf with 0
#endif
		}
		else
		{
#ifdef _ABUF_DEBUG_PAD_
			_inbuf = (uint8_t *)malloc(element_count * sizeof(T) + 8);
			if(_inbuf)
			{
				*(uint32_t*)_inbuf = abuf_mem_check_dword;
				*(uint32_t*)(_inbuf + element_count * sizeof(T) + 4) = abuf_mem_check_dword;
				_buf = (T *)(_inbuf + 4);
			}
			else
				_buf = NULL;
#else
			_buf = (T *)malloc(element_count * sizeof(T));
#endif
		}
		_size = _buf ? element_count : 0;
		abuf_mem_check();
	}

	abuf(const abuf &r) : abuf()
	{
		resize(r.size());
		if (size() > 0)
			memcpy(buf(), r.buf(), size());
	}

	abuf& operator =(const abuf &r)
	{
		resize(0);
		resize(r.size());
		if (size() > 0)
			memcpy(buf(), r.buf(), size());
		return *this;
	}

	~abuf()
	{
		abuf_mem_check();
#ifdef _ABUF_DEBUG_PAD_
		free(_inbuf);
		_inbuf = NULL;
#else
		free(_buf);
#endif
		_buf = NULL;
		_size = 0;
	}

	/** 
	 * first min(new_element_count, old_element_count) elements will
	 * remain unchanged after resize. 
	 * IMPORTANT: Any previously T* obtained through type 
	 * cast on abuf will be INVALIDATED after a resize(): 
	 * void f() 
	 * {
	 *  	abuf<char> buf(100);
	 *  	char *pbuf = buf;
	 *  	buf.resize(200);	// pbuf is INVALID now!
	 * } 
	 * @return int 0 if succeeded. other if failed (original buffer 
	 *  	   will be kept even if resize fails)
	 */
	int resize(size_t new_element_count)
	{
		abuf_mem_check();
		if(new_element_count > _size)
		{
#ifdef _ABUF_DEBUG_PAD_
			uint8_t *nbuf = (uint8_t *)realloc(_inbuf, new_element_count * sizeof(T) + 8);
#else
			uint8_t *nbuf = (uint8_t *)realloc(_buf, new_element_count * sizeof(T));
#endif
			if(!nbuf && new_element_count != 0)	// if realloc fails, return value is NULL and the input _buf would be untouched
				return 1;
#ifdef _ABUF_DEBUG_PAD_
			_inbuf = nbuf;
			*(uint32_t*)_inbuf = abuf_mem_check_dword;
			*(uint32_t*)(_inbuf + new_element_count * sizeof(T) + 4) = abuf_mem_check_dword;
			_buf = (T *)(_inbuf + 4);
#else
			_buf = (T *)nbuf;
#endif
			_size = new_element_count;
		}
		else if(new_element_count == 0)
		{
#ifdef _ABUF_DEBUG_PAD_
			free(_inbuf);
			_inbuf = NULL;
#else
			free(_buf);
#endif
			_buf = NULL;
			_size = 0;
		}
		abuf_mem_check();
		return 0;
	}

	size_t size() const
	{
		abuf_mem_check();
		return _size;
	}

	operator T *()
	{
		abuf_mem_check();
		return _buf;
	}

	operator const T *() const
	{
		abuf_mem_check();
		return _buf;
	}

	T *buf()
	{
		abuf_mem_check();
		return _buf;
	}

	const T *buf() const
	{
		abuf_mem_check();
		return _buf;
	}

//  operator bool() const
//  {
//  	return _size != 0;
//  }

	T &operator [](int n)
	{
		abuf_mem_check();
		return _buf[n];
	}

	bool isNull() const
	{
		abuf_mem_check();
		return _size == 0;
	}

	// Shallow copy
	int scopyFrom(const abuf<T> &src)
	{
		abuf_mem_check();
		return scopyFrom(src._buf, src.size());
	}

	int scopyFrom(const T *src, size_t size)
	{
		abuf_mem_check();
		if(resize(size))
			return 1;
		if(size > 0)
			memcpy(_buf, src, size * sizeof(T));
		abuf_mem_check();
		return 0;
	}
};

class abufchar : public abuf<char>
{
public:
	abufchar(const abufchar &copy)
	{
		abuf<char>();
		if(copy.isNull())
			return;
		if(0 == resize(copy.size()))
			memcpy(_buf, copy._buf, _size);
		abufchar_mem_check();
	}
	abufchar(const char *copy = NULL)
	{
		abuf<char>();
		if(!copy)
			return;
		if(0 == resize(strlen(copy) + 1))
			strcpy(_buf, copy);
		abufchar_mem_check();
	}
	int scopyFrom(const char *src)
	{
		abufchar_mem_check();
		int ret = abuf<char>::scopyFrom(src, strlen(src) + 1);	// it's strange that it wouldn't compile without "abuf<char>::"
		abufchar_mem_check();
		return ret;
	}
};

