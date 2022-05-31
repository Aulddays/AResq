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
	};
public:
	Remote();
	virtual ~Remote();
public:
	virtual int addDir(const char *path, size_t plen) = 0;
};


class RemoteDummy : public Remote
{
public:
	virtual int addDir(const char *path, size_t plen)
	{
		PELOG_LOG((PLV_INFO, "RemoteDummy addDir %.*s\n", plen, path));
		return 0;
	}
};