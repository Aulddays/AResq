#pragma once

#include <stdio.h>
#include <string>
#include <stdint.h>
#include <vector>
#include "utfconv.h"

#ifdef _WIN32
typedef utf16_t NCHART;
#define _NCT(x)      L ## x
#else
typedef char NCHART;
#define _NCT(x)      x
#endif

void Utf8toNchar(const char *utf8, abuf<NCHART> &ncs);

struct FsItem
{
	abuf<char> name;
	uint32_t time = 0;
	uint64_t size = 0;
	uint8_t flag = 0;
	inline bool getflag(int bit) const { return (flag & (1 << bit)) != 0; }
	inline void setflag(bool flag, int bit) { if (flag) this->flag |= (1u << bit); else this->flag &= ~(1u << bit); }
	inline bool isdir() const { return getflag(0); }
	inline void isdir(bool flag) { setflag(flag, 0); }
};

int CreateDir(const char *dir);

int BuildPath(const char *dir, const char *filename, abuf<NCHART> &path);
int BuildPath(const char **dir, size_t size, abufchar &path);
FILE *OpenFile(const char *filename, const NCHART *mode);
FILE *OpenFile(const char *dir, const char *filename, const NCHART *mode);

int ListDir(const abufchar &dir, std::vector<FsItem> &dirs, std::vector<FsItem> &files);

int pathCmpSt(const char *l, const char *r);		// keep case diffs near each other but different, used for sort
int pathCmpDp(const char *l, const char *r);	// completely case insensitive
int pathCmpMt(const char *l, const char *r);	// to match local file system, St if case sensitive, otherwise Dp
