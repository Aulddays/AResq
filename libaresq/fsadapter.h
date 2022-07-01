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

uint32_t getDirTime(const char *base, const char *dir, size_t dlen);
int getFileAttr(const char *base, const char *filename, size_t fnlen, uint32_t &ftime, uint64_t &fsize);

int buildPath(const char *dir, const char *filename, abuf<NCHART> &path);
int buildPath(const char *dir, const char *filename, size_t flen, abuf<NCHART> &path);
int buildPath(const char **dir, size_t size, abuf<char> &path);
int buildPath(const char *dir, size_t dirlen, const char *file, size_t filelen, abuf<char> &path);
inline int buildPath(const char *dir, const char *filename, abuf<char> &path) { const char *dirs[] = { dir, filename };  return buildPath(dirs, 2, path); }
FILE *OpenFile(const char *filename, const NCHART *mode);
FILE *OpenFile(const char *dir, const char *filename, const NCHART *mode);

int ListDir(const abufchar &dir, std::vector<FsItem> &dirs, std::vector<FsItem> &files);

int pathCmpSt(const char *l, const char *r);		// keep case diffs near each other but different, used for sort
int pathCmpDp(const char *l, const char *r);	// completely case insensitive
int pathCmpDp(const char *l, size_t ll, const char *r);	// completely case insensitive
int pathCmpMt(const char *l, const char *r);	// to match local file system, St if case sensitive, otherwise Dp
int pathCmpMt(const char *l, size_t ll, const char *r);	// to match local file system, St if case sensitive, otherwise Dp

int pathAbs2Rel(abufchar &path, const char *base);
const char *pathAbs2Rel(const char *path, const char *base);
int pathRel2Abs(abufchar &path, const char *base);

size_t splitPath(const char *path, size_t plen);
