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
	virtual int addDir(const char *rbase, const char *path);
	virtual int addFile(const char *lbase, const char *rbase, const char *path);
	virtual int delDir(const char *rbase, const char *path);
	virtual int delFile(const char *rbase, const char *path);
	virtual int putHist(const char *rbase, const char *path);
	virtual int getType(const char *fullpath);
	virtual int moveFile(const char *oldpath, const char *newpath, bool force);

protected:
	int addDir(const std::string &fullpath);
	int addFile(const std::string &lfullpath, const std::string &rfullpath);
	int delDir(const std::string &fullpath);
	int delFile(const std::string &fullpath);

	int isDir(const char *fullpath) { int type = getType(fullpath); return type == FT_DIR ? 1 : type >= 0 ? 0 : type; }

	int smbPutFile(const char *lfile, const char *rfile);

private:
	RemoteSmbData *d;

	friend class Remote;
	static Remote *fromConfig(const config_setting_t *config);
};

