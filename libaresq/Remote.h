#pragma once

#include <memory>
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
	static std::unique_ptr<Remote> fromConfig(const config_t *config);
public:
	virtual int connect() = 0;
	virtual void disconnect() = 0;
	virtual bool isConnected() const = 0;

	virtual int addDir(const char *rbase, const char *path) = 0;
	virtual int addFile(const char *lbase, const char *rbase, const char *path) = 0;
	virtual int delDir(const char *rbase, const char *path) = 0;
	virtual int delFile(const char *rbase, const char *path) = 0;
	virtual int putHist(const char *rbase, const char *path) = 0;
	// rename/move file or dir. force: delete destination if already exists
	virtual int moveFile(const char *rbase, const char *srcpath, const char *dstpath, bool force) = 0;

	enum { FT_NONE = 0, FT_FILE, FT_DIR, FT_LINK, FT_UNK };
	virtual int getType(const char *fullpath) = 0;
};


class RemoteDummy : public Remote
{
public:
	static std::unique_ptr<Remote> fromConfig(const config_setting_t *config)
	{
		return std::unique_ptr<Remote>(new RemoteDummy);
	}
	virtual int connect() override { return 0; }
	virtual void disconnect() override {}
	virtual bool isConnected() const override { return true; }
	virtual int addDir(const char *rbase, const char *path) override
	{
		PELOG_LOG((PLV_INFO, "RemoteDummy addDir %s/%s\n", rbase, path));
		return 0;
	}
	virtual int addFile(const char *lbase, const char *rbase, const char *path) override
	{
		PELOG_LOG((PLV_INFO, "RemoteDummy addFile %s/%s\n", rbase, path));
		return 0;
	}
	virtual int delDir(const char *rbase, const char *path) override
	{
		PELOG_LOG((PLV_INFO, "RemoteDummy delDir %s/%s\n", rbase, path));
		return 0;
	}
	virtual int delFile(const char *rbase, const char *path) override
	{
		PELOG_LOG((PLV_INFO, "RemoteDummy delFile %s/%s\n", rbase, path));
		return 0;
	}
	virtual int putHist(const char *rbase, const char *path) override
	{
		PELOG_LOG((PLV_INFO, "RemoteDummy putHist %s/%s\n", rbase, path));
		return 0;
	}
	virtual int moveFile(const char *rbase, const char *srcpath, const char *dstpath, bool force) override
	{
		PELOG_LOG((PLV_INFO, "RemoteDummy moveFile %s -> %s\n", srcpath, dstpath));
		return 0;
	}
	virtual int getType(const char *fullpath) override
	{
		return FT_NONE;
	}
};
