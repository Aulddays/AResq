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

int BuildPath(const char *dir, const char *filename, abuf<utf16_t> &path)
{
	size_t outl = utf8to16_len((const utf8_t *)dir) + 1 + utf8to16_len((const utf8_t *)filename);
	path.resize(outl + 1);
	outl = 0;
	utf8to16((const utf8_t *)dir, path, outl);
	path[outl++] = DIRSEP;
	utf8to16((const utf8_t *)filename, path, outl);
	return 0;
}


FILE *OpenFile(const char *dir, const char *filename, const wchar_t *mode)
{
	abuf<utf16_t> path;
	BuildPath(dir, filename, path);
	return _wfopen(path, mode);
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
	assert(cplen == dirlen);
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
		items.resize(items.size() + 1);
		FsItem &item = items.back();
		utf16to8(ffd.cFileName, item.name);
		item.size = ((uint64_t)ffd.nFileSizeHigh * (MAXDWORD + (uint64_t)1)) + ffd.nFileSizeLow;
		item.time = (uint32_t)(((uint64_t)ffd.ftLastWriteTime.dwHighDateTime << 32 | ffd.ftLastWriteTime.dwLowDateTime) /
			10000000 - 11644473600LL);
		item.isdir(ffd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY ? true : false);
	} while (FindNextFileW(hFind, &ffd));

	// sort
	std::sort(items.begin(), items.end(),
		[](const FsItem &l, const FsItem &r) { return pathCmpSt(l.name, r.name); });

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


// end of win32 specific
#elif defined __linux__
static const char DIRSEP = '/';
#endif	// end of linux specific

int BuildPath(const char **dir, size_t size, abufchar &path)
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
		for (const char *pi = dir[size]; *pi;)
			*po++ = *pi++;
		if (i != size - 1)
			*po++ = DIRSEP;
	}
	*po = 0;
	return 0;
}

int pathCmpSt(const char *l, const char *r)		// keep case diffs near each other, used for sort
{
	// compare case insensitively first
	for (const char *tl = l, *tr = r; true; tl++, tr++)
	{
		int cl = *tl >= 'A' && *tl <= 'Z' ? *tl + ('a' - 'A') : *tl;
		int cr = *tr >= 'A' && *tr <= 'Z' ? *tr + ('a' - 'A') : *tr;
		if (cl != cr)
			return cl - cr;
		if (!cl)
			break;
	}
	// equal case insensitively if reaches here, compare again case sensitively
	while (*l && *l == *r)
		l++, r++;
	return l - r;
}

int pathCmpDp(const char *l, const char *r)	// completely case insensitive
{
	const unsigned char *tl = (const unsigned char *)l, *tr = (const unsigned char *)r;
	for (const char *tl = l, *tr = r; true; tl++, tr++)
	{
		int cl = *tl >= 'A' && *tl <= 'Z' ? *tl + ('a' - 'A') : *tl;
		int cr = *tr >= 'A' && *tr <= 'Z' ? *tr + ('a' - 'A') : *tr;
		if (!cl || cl != cr)
			return cl - cr;
	}
	assert(0);	// should no reach here
	return 0;
}