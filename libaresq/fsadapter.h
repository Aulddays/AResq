#pragma once

#include <stdio.h>
#include <string>
#include <stdint.h>
#include <vector>

#ifdef _WIN32
#include "utfconv.h"
typedef utf16_t NCHART;
#define _NCT(x)      L ## x
#define time64 _time64
#define ftell _ftelli64
#define fseek _fseeki64
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
	inline bool isdir() const { return getflag(0); }
	inline void isdir(bool flag) { setflag(flag, 0); }
	inline bool isignore() const { return getflag(1); }
	inline void isignore(bool flag) { setflag(flag, 1); }
private:
	inline bool getflag(int bit) const { return (flag & (1 << bit)) != 0; }
	inline void setflag(bool flag, int bit) { if (flag) this->flag |= (1u << bit); else this->flag &= ~(1u << bit); }
};

class FileHandle
{
	FILE *fp;
public:
	FileHandle(FILE *r = NULL) : fp(r){}
	~FileHandle() { close(); }
	void close() { if (fp) fclose(fp); fp = NULL; }
	FILE *operator =(FILE *r) { close(); fp = r; return fp; }
	operator FILE *() { return fp; }
};

int CreateDir(const char *dir);

uint64_t getDirTime(const char *base, const char *dir, size_t dlen);
int getFileAttr(const char *base, const char *filename, size_t fnlen, uint64_t &ftime, uint64_t &fsize);

int buildPath(const char *dir, const char *filename, abuf<NCHART> &path);
int buildPath(const char *dir, const char *filename, size_t flen, abuf<NCHART> &path);
int buildPath(const char **dir, size_t size, abuf<char> &path);
int buildPath(const char *dir, size_t dirlen, const char *file, size_t filelen, abuf<char> &path);
inline int buildPath(const char *dir, const char *filename, abuf<char> &path) { const char *dirs[] = { dir, filename };  return buildPath(dirs, 2, path); }
FILE *OpenFile(const char *filename, const NCHART *mode);
FILE *OpenFile(const char *dir, const char *filename, const NCHART *mode);

int ListDir(const abufchar &dir, std::vector<FsItem> &items);

int pathCmpSt(const char *l, const char *r);		// keep case diffs near each other but different, used for sort
int pathCmpDp(const char *l, const char *r);	// completely case insensitive
int pathCmpDp(const char *l, size_t ll, const char *r);	// completely case insensitive
int pathCmpMt(const char *l, const char *r);	// to match local file system, St if case sensitive, otherwise Dp
int pathCmpMt(const char *l, size_t ll, const char *r);	// to match local file system, St if case sensitive, otherwise Dp

int pathAbs2Rel(abufchar &path, const char *base);
const char *pathAbs2Rel(const char *path, const char *base);
int pathRel2Abs(abufchar &path, const char *base);

size_t splitPath(const char *path, size_t plen);
