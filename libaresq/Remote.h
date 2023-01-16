#pragma once

#include "pe_log.h"
#define LIBCONFIG_STATIC
#include "libconfig/libconfig.h"

#define AR_RUNTIMEDIR ".aresq"
#define AR_TMPDIR AR_RUNTIMEDIR "/tmp"
#define AR_HISTDIR AR_RUNTIMEDIR "/hist"

class Remote
{
public:
	Remote() {}
	virtual ~Remote() {}
	static Remote *fromConfig(const config_t *config);
public:
	virtual int addDir(const char *rbase, const char *path) = 0;
	virtual int addFile(const char *lbase, const char *rbase, const char *path) = 0;
	virtual int delDir(const char *rbase, const char *path) = 0;
	virtual int delFile(const char *rbase, const char *path) = 0;

	enum { FT_NONE = 0, FT_FILE, FT_DIR, FT_LINK, FT_UNK };
	virtual int getType(const char *fullpath) = 0;
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
