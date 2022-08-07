#include "stdafx.h"
#include "RemoteSmb.h"

#include <inttypes.h>
#include <time.h>
#include <stdio.h>
#include <fcntl.h>
#include <algorithm>
#include <string>
#include <vector>
#include <utility>

#include "auto_buf.hpp"
#include "libsmb2/smb2.h"
#include "libsmb2/libsmb2.h"
#ifdef _MSC_VER
#	include "libsmb2/msvc/poll.h"
#endif

class SmbHandle
{
private:
	smb2_context *smb = NULL;
	bool connected = false;
	std::string server;
	std::string share;
	std::string user;
	std::string password;
	SmbHandle(SmbHandle &r);	// disable copy
	void operator =(SmbHandle &r);
public:
	void clear()
	{
		if (connected && smb)
			smb2_disconnect_share(smb);
		connected = false;
		if (smb)
			smb2_destroy_context(smb);
		smb = NULL;
		server = share = user = password = "";
	}
	SmbHandle(){}
	~SmbHandle(){ clear(); }
	int init(const char *server, const char *share, const char *user, const char *password)
	{
		clear();
		smb = smb2_init_context();
		if (!smb)
			PELOG_ERROR_RETURN((PLV_ERROR, "RemoteSmb smb init failed\n"), RemoteSmb::EINTERNAL);
		this->server = server;
		this->share = share;
		this->user = user;
		this->password = password;
		smb2_set_user(smb, this->user.c_str());
		smb2_set_password(smb, this->password.c_str());
		smb2_set_security_mode(smb, SMB2_NEGOTIATE_SIGNING_ENABLED);
		smb2_set_version(smb, SMB2_VERSION_ANY2);
		return RemoteSmb::OK;
	}
	int connect()
	{
		if (connected)
			PELOG_ERROR_RETURN((PLV_WARNING, "Already connected\n"), RemoteSmb::OK);
		int res = smb2_connect_share(smb, server.c_str(), share.c_str(), NULL);
		if (res == -EIO)
			PELOG_ERROR_RETURN((PLV_ERROR, "connect failed network %d\n", res), RemoteSmb::DISCONNECTED);
		else if (res == -ECONNREFUSED || res == -ENOENT)
			PELOG_ERROR_RETURN((PLV_ERROR, "connect credential error %d\n", res), RemoteSmb::EPARAM);
		else if (res < 0)
			PELOG_ERROR_RETURN((PLV_ERROR, "connect error %d\n", res), RemoteSmb::EPARAM);
		connected = true;
		return RemoteSmb::OK;
	}
	void disconnect()
	{
		if (connected)
			smb2_disconnect_share(smb);
		connected = false;
	}
	operator smb2_context *() { return smb; }
	bool isconnected() const { return connected; }
};

struct RemoteSmbData
{
	SmbHandle smb;
};

class SmbDir
{
	smb2dir *dir;
	smb2_context *smb;
public:
	SmbDir(smb2_context *smb2 = NULL) : dir(NULL), smb(smb2){}
	void setSmb(smb2_context *smb2) { smb = smb2; }
	void close() { if (dir) smb2_closedir(smb, dir); dir = NULL; }
	smb2dir *operator =(smb2dir *r) { close(); dir = r; return dir; }
	operator smb2dir *() { return dir; }
};

#ifdef _WIN32
static class _WSAGuard
{
public:
	_WSAGuard()
	{
		WORD wsaver = MAKEWORD(2, 2);
		WSADATA wsaData;
		WSAStartup(wsaver, &wsaData);
	}
	~_WSAGuard()
	{
		WSACleanup();
	}
} _wsaguard;
#endif

RemoteSmb::RemoteSmb()
{
	d = new RemoteSmbData;
}


RemoteSmb::~RemoteSmb()
{
	delete d;
	d = NULL;
}

int RemoteSmb::init()
{
	int res = OK;
	if ((res = d->smb.init("example.com", "test", "test", "test")) != OK)
		return res;

	if ((res = d->smb.connect()) != OK)
		return res;

	return 0;
}

class SmbFile
{
	smb2fh *fp;
	smb2_context *smb;
public:
	SmbFile(smb2_context *smb2 = NULL) : fp(NULL), smb(smb2){}
	void setSmb(smb2_context *smb2) { smb = smb2; }
	void close() { if (fp) smb2_close(smb, fp); fp = NULL; }
	smb2fh *operator =(smb2fh *r) { close(); fp = r; return fp; }
	operator smb2fh *() { return fp; }
};

class FileHandle
{
	FILE *fp;
public:
	FileHandle(FILE *r = NULL) : fp(r){}
	void close(){ if (fp) fclose(fp); fp = NULL; }
	FILE *operator =(FILE *r) { close(); fp = r; return fp; }
	operator FILE *() { return fp; }
};

struct SmbPutInfo
{
	int status = 0;
	smb2_context *smb = NULL;
	FileHandle lfp;
	SmbFile rfp;
	uint64_t writesize = 0;
	uint64_t realsize = 0;
	size_t chunksize = 0;
	int *signal = NULL;
	abuf<char> buf;
};

inline size_t round1K(size_t size)
{
	return (size + (1024 - 1)) & ~((size_t)(1024 - 1));
}

void onSmbPutChunk(struct smb2_context *smb2, int status, void *command_data, SmbPutInfo *info)
{
	if (status < 0)
	{
		info->status = status;
		PELOG_ERROR_RETURNVOID((PLV_ERROR, "Upload failed 4 %d\n", status));
	}
	if (status == 0 || status != info->chunksize)
		PELOG_ERROR_RETURNVOID((PLV_ERROR, "Upload failed 5\n"));
	PELOG_LOG((PLV_DEBUG, "%d bytes put. total %"PRIu64"\n", status, info->writesize));
	info->writesize += status;
	info->chunksize = fread(info->buf, 1, info->buf.size(), info->lfp);
	AuVerify(info->writesize == info->realsize || info->chunksize == 0);		// writesize != realsize only occurs when last chunk has just been put
	if (info->chunksize == 0)	// no more data
	{
		info->status = 1;
		return;
	}
	info->realsize += info->chunksize;
	info->chunksize = round1K(info->chunksize);	// Some server may fail on certain chunksizes. write extra data and truncate as workaround
	int res = 0;
	if ((res = smb2_pwrite_async(info->smb, info->rfp, (const uint8_t *)info->buf.buf(), info->chunksize,
		info->writesize, (smb2_command_cb)onSmbPutChunk, info)) < 0)
	{
		info->status = res;
		PELOG_ERROR_RETURNVOID((PLV_ERROR, "Upload failed 6 %d\n", res));
	}
}


int smbPutFile(const char *lfile, const char *rfile, RemoteSmbData *d)
{
	SmbPutInfo info;
	info.smb = d->smb;

	if (!(info.lfp = fopen(lfile, "rb")))
		PELOG_ERROR_RETURN((PLV_ERROR, "Cannot read file %s\n", lfile), RemoteSmb::EPARAM);
	info.rfp.setSmb(info.smb);
	if (!(info.rfp = smb2_open(info.smb, rfile, O_WRONLY | O_CREAT)))
		PELOG_ERROR_RETURN((PLV_ERROR, "Cannot write remote file\n"), RemoteSmb::EPARAM);

	int res = 0;
	uint32_t maxchunksize = smb2_get_max_write_size(info.smb);
	PELOG_LOG((PLV_DEBUG, "maxchunksize %d\n", maxchunksize));
	info.buf.resize(std::min(1024 * 1024u, maxchunksize));
	info.chunksize = fread(info.buf, 1, info.buf.size(), info.lfp);
	info.realsize = info.chunksize;
	info.chunksize = round1K(info.chunksize);	// Some server may fail on certain chunksizes. write extra data and the truncate as workaround
	if (info.realsize < info.chunksize)
		memset(info.buf + info.realsize, 0, info.chunksize - info.realsize);
	if (info.chunksize == 0)
		info.status = 1;
	else if ((res = smb2_pwrite_async(d->smb, info.rfp, (const uint8_t *)info.buf.buf(), info.chunksize,
			info.writesize, (smb2_command_cb)onSmbPutChunk, &info)) < 0)
		PELOG_ERROR_RETURN((PLV_ERROR, "Upload failed 1 %d\n", res), RemoteSmb::DISCONNECTED);

	while (info.status == 0)
	{
		pollfd pfd = { 0 };
		pfd.fd = smb2_get_fd(info.smb);
		pfd.events = smb2_which_events(info.smb);
		if ((res = poll(&pfd, 1, 1000)) < 0)
			PELOG_ERROR_RETURN((PLV_ERROR, "Upload failed 2 %d\n", res), RemoteSmb::DISCONNECTED);
		if (pfd.revents == 0)
			continue;
		if ((res = smb2_service(info.smb, pfd.revents)) < 0)
			PELOG_ERROR_RETURN((PLV_ERROR, "Upload failed 3 %d: %s\n", res, smb2_get_error(info.smb)), RemoteSmb::DISCONNECTED);
	}

	info.rfp.close();
	// always truncate, even if no extra data were written, in case of larger version of this file already exists
	if (info.status > 0 && (res = smb2_truncate(info.smb, rfile, info.realsize)) < 0)
		PELOG_ERROR_RETURN((PLV_ERROR, "Upload failed 7 %d: %s\n", res, smb2_get_error(info.smb)), RemoteSmb::DISCONNECTED);

	if (info.status > 0)
		PELOG_ERROR_RETURN((PLV_VERBOSE, "PUTDONE %"PRIu64" %s -> %s\n", info.realsize, lfile, rfile), Remote::OK);
	return Remote::DISCONNECTED;
}

int RemoteSmb::rawPutFile(const char *lfile, const char *rfile)
{
	return smbPutFile(lfile, rfile, d);

	//SmbPutInfo info;
	//info.smb = d->smb;
	//if (!(info.lfp = fopen(lfile, "rb")))
	//	PELOG_ERROR_RETURN((PLV_ERROR, "Cannot read file %s\n", lfile), RemoteSmb::EPARAM);
	//if (!(info.rfp = smb2_open(d->smb, rfile, O_WRONLY | O_CREAT)))
	//	PELOG_ERROR_RETURN((PLV_ERROR, "Cannot write remote file\n"), RemoteSmb::EPARAM);

	//int res = 0;
	//info.buf.resize(1024 * 1024 * 10);
	//size_t chunklen = 0;
	//while ((chunklen = fread(info.buf, 1, info.buf.size(), info.lfp)) > 0)
	//{
	//	if ((res = smb2_write(info.smb, info.rfp, (uint8_t *)info.buf.buf(), chunklen)) < 0)
	//		PELOG_ERROR_RETURN((PLV_ERROR, "Upload filed 1 %d: %s\n", res, smb2_get_error(info.smb)), RemoteSmb::DISCONNECTED);
	//}
	//return 0;
}

int smbIsDir(SmbHandle &smb, const char *path)
{
	smb2_stat_64 stat;
	int res = smb2_stat(smb, path, &stat);
	if (res == -ENOENT)	// not found
		return 0;
	if (res < 0)
		PELOG_ERROR_RETURN((PLV_ERROR, "stat failed %d: %s\n", res, path), res);
	return stat.smb2_type == SMB2_TYPE_DIRECTORY ? 1 : 0;
}

int RemoteSmb::addDir(const char *path, size_t plen)
{
	if (!d->smb.isconnected())
		PELOG_ERROR_RETURN((PLV_ERROR, "ADDDIR remote disconnected: %.*s\n", plen, path), DISCONNECTED);
	std::string tpath(path, plen);
	PELOG_LOG((PLV_DEBUG, "to ADDDIR %s\n", tpath.c_str()));
	int res = smb2_mkdir(d->smb, tpath.c_str());
	if (res == -EEXIST)
	{
		res = smbIsDir(d->smb, tpath.c_str());
		if (res < 0)
			PELOG_ERROR_RETURN((PLV_ERROR, "ADDDIR failed %d: %s\n", res, tpath.c_str()), EPARAM);
		if (res > 0)
			PELOG_ERROR_RETURN((PLV_VERBOSE, "Already exists: %s\n", tpath.c_str()), OK);
		PELOG_LOG((PLV_WARNING, "DIR name conflict. deleting the existing file. %s\n", tpath.c_str()));
		if ((res = smb2_unlink(d->smb, tpath.c_str())) == 0)
			res = smb2_mkdir(d->smb, tpath.c_str());
	}
	else if (res == -ENOENT)
	{
		PELOG_LOG((PLV_VERBOSE, "Creating parent dir: %s\n", tpath.c_str()));
		size_t sep = tpath.rfind('/');
		if (sep == tpath.npos)
			PELOG_ERROR_RETURN((PLV_ERROR, "ADDDIR create parent failed: %s\n", tpath.c_str()), EPARAM);
		if ((res = addDir(tpath.c_str(), sep)) != OK)
			PELOG_ERROR_RETURN((PLV_ERROR, "ADDDIR create parent failed: %s\n", tpath.c_str()), EPARAM);
		res = smb2_mkdir(d->smb, tpath.c_str());
	}
	if (res != 0)
		PELOG_ERROR_RETURN((PLV_ERROR, "ADDDIR failed %d: %s\n", res, nterror_to_str(res)), EPARAM);
	PELOG_LOG((PLV_VERBOSE, "DIR added: %s\n", tpath.c_str()));
	return OK;
}

int RemoteSmb::addFile(const char *path, size_t plen)
{
	if (!d->smb.isconnected())
		PELOG_ERROR_RETURN((PLV_ERROR, "ADDFILE remote disconnected: %.*s\n", plen, path), DISCONNECTED);
	return OK;
}

int RemoteSmb::delDir(const char *path, size_t plen)
{
	if (!d->smb.isconnected())
		PELOG_ERROR_RETURN((PLV_ERROR, "DELDIR remote disconnected: %.*s\n", plen, path), DISCONNECTED);
	if (plen == 0)
		PELOG_ERROR_RETURN((PLV_ERROR, "DELDIR invalid dir name\n"), DISCONNECTED);
	std::string tpath(path, plen);

	// del contents
	int res = OK;
	bool empty = false;
	SmbDir dir(d->smb);
	for (int retry = 0; retry < 3; ++retry)
	{
		dir = smb2_opendir(d->smb, tpath.c_str());
		if (!dir)
			PELOG_ERROR_RETURN((PLV_WARNING, "dir not found\n"), OK);
		struct smb2dirent *ent = NULL;
		std::vector<std::pair<std::string, bool>> dcont;
		while ((ent = smb2_readdir(d->smb, dir)))
		{
			if (strcmp(ent->name, ".") == 0 || strcmp(ent->name, "..") == 0)
				continue;
			dcont.emplace_back(ent->name, ent->st.smb2_type == SMB2_TYPE_DIRECTORY);
		}
		dir.close();
		if (dcont.empty())
		{
			empty = true;
			break;
		}
		for (const std::pair<std::string, bool> &item : dcont)
		{
			if (item.second)
				res = delDir((tpath + '/' + item.first).c_str(), tpath.length() + 1 + item.first.length());
			else
				res = delFile((tpath + '/' + item.first).c_str(), tpath.length() + 1 + item.first.length());
			if (res != OK)
				return res;
		}
	}

	if (!empty)
		PELOG_ERROR_RETURN((PLV_ERROR, "DELDIR: del contents failed %s\n", tpath.c_str()), EINTERNAL);

	PELOG_LOG((PLV_DEBUG, "to DELDIR %s\n", tpath.c_str()));
	int res = smb2_rmdir(d->smb, tpath.c_str());
	if (res < 0 && res != -ENOENT)
		PELOG_ERROR_RETURN((PLV_ERROR, "DELDIR failed %d: %s\n", res, tpath.c_str()), EPARAM);

	// verify
	res = smbIsDir(d->smb, tpath.c_str());
	if (res != 0)
		PELOG_ERROR_RETURN((PLV_ERROR, "DELDIR failed %d %s\n", res, tpath.c_str()), EPARAM);

	return OK;
}

int RemoteSmb::delFile(const char *path, size_t plen)
{
	if (!d->smb.isconnected())
		PELOG_ERROR_RETURN((PLV_ERROR, "DELFILE remote disconnected: %.*s\n", plen, path), DISCONNECTED);
	std::string tpath(path, plen);
	int res = smb2_unlink(d->smb, tpath.c_str());
	if (res < 0)
		PELOG_ERROR_RETURN((PLV_ERROR, "DELFILE failed %d: %s\n", res, tpath.c_str()), EPARAM);
	return OK;
}

