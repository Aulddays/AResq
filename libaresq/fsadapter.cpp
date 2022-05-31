#include "stdafx.h"

#include "fsadapter.h"
#include <algorithm>

#include "pe_log.h"

#ifdef _WIN32
#include <Windows.h>
#include <tchar.h>

static const char DIRSEP = '\\';

void Utf8toNchar(const char *utf8, abuf<NCHART> &ncs)
{
	utf8to16((const utf8_t *)utf8, ncs);
}

int CreateDir(const char *dir)
{
	// check existance
	abuf<utf16_t> pathbuf;
	utf8to16((const utf8_t *)dir, pathbuf);
	DWORD dwAttrib = GetFileAttributesW(pathbuf);
	if (INVALID_FILE_ATTRIBUTES != dwAttrib && !(dwAttrib & FILE_ATTRIBUTE_DIRECTORY))
		return -1;
	if (INVALID_FILE_ATTRIBUTES != dwAttrib && (dwAttrib & FILE_ATTRIBUTE_DIRECTORY))
		return 0;
	if (!CreateDirectoryW(pathbuf, NULL))
		return -2;
	return 0;
}

int buildPath(const char *dir, const char *filename, abuf<utf16_t> &path)
{
	size_t outl = utf8to16_len((const utf8_t *)dir) + 1 + utf8to16_len((const utf8_t *)filename) + 1;
	path.resize(outl + 1);
	outl = 0;
	utf8to16((const utf8_t *)dir, path, outl);
	if (*dir)
		path[outl++] = DIRSEP;
	utf8to16((const utf8_t *)filename, path, outl);
	return 0;
}


int buildPath(const char *dir, const char *filename, size_t flen, abuf<utf16_t> &path)
{
	size_t outl = utf8to16_len(dir) + 1 + utf8to16_len(filename, flen);
	path.resize(outl + 1);
	outl = 0;
	utf8to16(dir, path, outl);
	if (*dir)
		path[outl++] = DIRSEP;
	utf8to16((const utf8_t *)filename, flen, path, outl);
	return 0;
}


FILE *OpenFile(const char *dir, const char *filename, const wchar_t *mode)
{
	abuf<utf16_t> path;
	buildPath(dir, filename, path);
	return _wfopen(path, mode);
}

inline uint32_t filetime2Timet(FILETIME ft)
{
	return (uint32_t)(((uint64_t)ft.dwHighDateTime << 32 | ft.dwLowDateTime) / 10000000 - 11644473600LL);
}

int ListDir(const abufchar &u8dir, std::vector<FsItem> &dirs, std::vector<FsItem> &files)
{
	dirs.clear();
	files.clear();

	// convert to utf16 and append "\\*" to the end for `dir`
	size_t dirlen = utf8to16_len(u8dir);
	abuf<wchar_t> dir(dirlen + 3);
	size_t cplen = 0;
	utf8to16(u8dir, dir, cplen);
	AAssert(cplen == dirlen);
	dir[dirlen] = DIRSEP;
	dir[dirlen + 1] = '*';
	dir[dirlen + 2] = 0;

	std::vector<FsItem> items;
	WIN32_FIND_DATAW ffd;
	HANDLE hFind = FindFirstFileW(dir, &ffd);
	dir[dirlen] = 0;
	if (hFind == INVALID_HANDLE_VALUE)
		return -1;
	do
	{
		if (ffd.dwFileAttributes == INVALID_FILE_ATTRIBUTES)
			continue;
		if (ffd.cFileName[0] == L'.' && (ffd.cFileName[1] == 0 ||
			ffd.cFileName[1] == L'.' && ffd.cFileName[2] == 0))
			continue;	// exluce "." and ".." dirs
		items.resize(items.size() + 1);
		FsItem &item = items.back();
		utf16to8(ffd.cFileName, item.name);
		item.size = ((uint64_t)ffd.nFileSizeHigh * (MAXDWORD + (uint64_t)1)) + ffd.nFileSizeLow;
		item.isdir(ffd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY ? true : false);
		item.time = filetime2Timet(item.isdir() ? ffd.ftCreationTime : ffd.ftLastWriteTime);
	} while (FindNextFileW(hFind, &ffd));

	// sort
	std::sort(items.begin(), items.end(),
		[](const FsItem &l, const FsItem &r) { return pathCmpSt(l.name, r.name) < 0; });

	// dedupe and split
	const char *lname = "";
	for (const auto &item : items)
	{
		if (pathCmpDp(item.name, lname) == 0)
		{
			PELOG_LOG((PLV_WARNING, "DUP-CASE %s: %s. drop\n", u8dir.buf(), item.name.buf()));
			continue;
		}
		lname = item.name;
		(item.isdir() ? dirs : files).push_back(item);
	}
	return 0;
}

int pathCmpMt(const char *l, const char *r)	// case insensitive match on windows
{
	return pathCmpDp(l, r);
}

int pathCmpMt(const char *l, size_t ll, const char *r)	// case insensitive match on windows
{
	return pathCmpDp(l, ll, r);
}

uint32_t getDirTime(const char *base, const char *dir, size_t dlen)
{
	abuf<wchar_t> path;
	buildPath(base, dir, dlen, path);
	WIN32_FILE_ATTRIBUTE_DATA fad;
	if (GetFileAttributesExW(path, GetFileExInfoStandard, &fad))
		return filetime2Timet(fad.ftCreationTime);
	return 0;
}


// end of win32 specific
#elif defined __linux__
static const char DIRSEP = '/';
#endif	// end of linux specific

int buildPath(const char **dir, size_t size, abuf<char> &path)
{
	if (size == 0)
	{
		path.resize(1);
		path[0] = 0;
		return 0;
	}
	// calculate output len
	size_t outl = 0;
	for (size_t i = 0; i < size; ++i)
		outl += strlen(dir[i]);	// paths
	outl += size - 1;	// sep chars
	path.resize(outl + 1);	// +1 for '\0'
	// concat
	char *po = path;
	for (size_t i = 0; i < size; ++i)
	{
		for (const char *pi = dir[i]; *pi;)
			*po++ = *pi++;
		if (i != size - 1 && *dir[i])
			*po++ = DIRSEP;
	}
	*po = 0;
	return 0;
}

int pathCmpSt(const char *l, const char *r)		// keep case diffs near each other, used for sort
{
	// compare case insensitively first
	for (const unsigned char *tl = (const unsigned char *)l, *tr = (const unsigned char *)r; true; tl++, tr++)
	{
		int cl = *tl >= 'A' && *tl <= 'Z' ? *tl + ('a' - 'A') : *tl;
		int cr = *tr >= 'A' && *tr <= 'Z' ? *tr + ('a' - 'A') : *tr;
		if (cl != cr)
			return cl - cr;
		if (!cl)
			break;
	}
	// if arrives here, equal case insensitively, compare again case sensitively
	while (*l && *l == *r)
		l++, r++;
	return (unsigned char)*l - (unsigned char)*r;
}

int pathCmpDp(const char *l, const char *r)	// completely case insensitive
{
	for (const unsigned char *tl = (const unsigned char *)l, *tr = (const unsigned char *)r; true; tl++, tr++)
	{
		int cl = *tl >= 'A' && *tl <= 'Z' ? *tl + ('a' - 'A') : *tl;
		int cr = *tr >= 'A' && *tr <= 'Z' ? *tr + ('a' - 'A') : *tr;
		if (!cl || cl != cr)
			return cl - cr;
	}
	AAssert(0);	// should no reach here
	return 0;
}

int pathCmpDp(const char *l, size_t ll, const char *r)	// completely case insensitive
{
	for ( ; ll > 0; l++, r++, ll--)
	{
		unsigned char cl = *l >= 'A' && *l <= 'Z' ? *l + ('a' - 'A') : *l;
		unsigned char cr = *r >= 'A' && *r <= 'Z' ? *r + ('a' - 'A') : *r;
		if (!cr || cl != cr)
			return cl - cr;
	}
	AAssert(ll == 0);
	return 0 - (unsigned char)*r;
}

int pathAbs2Rel(abufchar &path, const char *base)
{
	size_t lp = 0, lb = 0;
	for (const char *pb = base, *pp = path; *pb; ++lb, ++pp, ++pb)
	{
		if (*pp != *pb)	// path not starts with base
			return -1;
	}
	AAssert(base[lb] == 0 && path.size() > lb && path[lb] == DIRSEP);
	for (char *pi = path + lb + 1, *po = path; true; ++pi, ++po)
	{
		*po = *pi;
		if (!*pi)
		{
			path.resize(po - path + 1);
			break;
		}
	}
	return 0;
}

const char *pathAbs2Rel(const char *path, const char *base)
{
	size_t lp = 0, lb = 0;
	for ( ; *base; ++path, ++base)
	{
		if (*base != *path)	// path not starts with base
			return NULL;
	}
	if (!*path)
		return path;
	AAssert(*base == 0 && *path == DIRSEP);
	return path + 1;
}

int pathRel2Abs(abufchar &path, const char *base)
{
	size_t lp = strlen(path);
	size_t lb = strlen(base);
	path.resize(lp + lb + 2);
	memmove(path + lb + 1, path, lp + 1);
	path[lb] = DIRSEP;
	memcpy(path, base, lb);
	return 0;
}

// return lenth of path
size_t splitPath(const char *path, size_t plen)
{
	if (plen == 0)
		return 0;
	AAssert(path[0] != DIRSEP && path[plen - 1] != DIRSEP);
	for (--plen; plen > 0; --plen)
	{
		if (path[plen] == DIRSEP)
			return plen;
	}
	return plen;
}