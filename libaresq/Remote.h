#pragma once

#include "pe_log.h"

class Remote
{
public:
	enum
	{
		OK = 0,
		DISCONNECTED = -1,
		CONFLICT = -2,
		EPARAM = -3,		// parameter error
		EINTERNAL = -4,	// internal error
		ECANCELE = -5,
	};
public:
	Remote();
	virtual ~Remote();
public:
	virtual int addDir(const char *path, size_t plen) = 0;
	virtual int addFile(const char *path, size_t plen) = 0;
	virtual int delDir(const char *path, size_t plen) = 0;
	virtual int delFile(const char *path, size_t plen) = 0;
};


class RemoteDummy : public Remote
{
public:
	virtual int addDir(const char *path, size_t plen)
	{
		PELOG_LOG((PLV_INFO, "RemoteDummy addDir %.*s\n", plen, path));
		return 0;
	}
	virtual int addFile(const char *path, size_t plen)
	{
		PELOG_LOG((PLV_INFO, "RemoteDummy addFile %.*s\n", plen, path));
		return 0;
	}
	virtual int delDir(const char *path, size_t plen)
	{
		PELOG_LOG((PLV_INFO, "RemoteDummy delDir %.*s\n", plen, path));
		return 0;
	}
	virtual int delFile(const char *path, size_t plen)
	{
		PELOG_LOG((PLV_INFO, "RemoteDummy delFile %.*s\n", plen, path));
		return 0;
	}
};
