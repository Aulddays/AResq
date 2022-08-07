#pragma once
#include "Remote.h"

struct RemoteSmbData;

class RemoteSmb : public Remote
{
public:
	RemoteSmb();
	virtual ~RemoteSmb();
	int init();
	virtual int addDir(const char *path, size_t plen);
	virtual int addFile(const char *path, size_t plen);
	virtual int delDir(const char *path, size_t plen);
	virtual int delFile(const char *path, size_t plen);

	int rawPutFile(const char *lfile, const char *rfile);

private:
	RemoteSmbData *d;
};

