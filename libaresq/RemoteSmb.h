#pragma once
#include "Remote.h"
#include "libconfig/libconfig.h"
#include "string"

struct RemoteSmbData;

class RemoteSmb : public Remote
{
public:
	RemoteSmb();
	virtual ~RemoteSmb();
	int init(const char *server, const char *share, const char *user, const char *password, const char *path);
	virtual int connect();
	virtual void disconnect();
	virtual bool isConnected() const;
	virtual int addDir(const char *rbase, const char *path);
	virtual int addFile(const char *lbase, const char *rbase, const char *path);
	virtual int delDir(const char *rbase, const char *path);
	virtual int delFile(const char *rbase, const char *path);
	virtual int putHist(const char *rbase, const char *path);
	virtual int getType(const char *rbase, const char *path);
	virtual int moveFile(const char *rbase, const char *srcpath, const char *dstpath, bool force);

protected:
	// caller holds mutex
	int addDirNolock(const std::string &fullpath);
	int addFileNolock(const std::string &lfullpath, const std::string &rfullpath);
	int delDirNolock(const std::string &fullpath);
	int delFileNolock(const std::string &fullpath);
	int moveFileNolock(const std::string &fullsrcpath, const std::string &fulldstpath, bool force);
	int getTypeNolock(const char *fullpath);

	int isDirNolock(const char *fullpath) { int type = getTypeNolock(fullpath); return type == FT_DIR ? 1 : type >= 0 ? 0 : type; }

	int smbPutFileNolock(const char *lfile, const char *rfile);

private:
	RemoteSmbData *d;

	friend class Remote;
	static std::unique_ptr<Remote> fromConfig(const config_setting_t *config);
};

