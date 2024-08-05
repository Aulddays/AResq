#include "stdafx.h"
#include "RemoteSmb.h"

#include <inttypes.h>
#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <algorithm>
#include <string>
#include <vector>
#include <utility>
#include <memory>
#include <chrono>
#include <process.h>

#include "Aresq.h"
#include "auto_buf.hpp"
#include "fsadapter.h"
#include "libsmb2/smb2.h"
#include "libsmb2/libsmb2.h"
#ifdef _MSC_VER
#	include "libsmb2/msvc/poll.h"
#	define snprintf _snprintf
#endif

class SmbHandle
{
	friend class RemoteSmb;
private:
	smb2_context *smb = NULL;
	bool connected = false;
	std::string server;
	std::string share;
	std::string user;
	std::string password;
	std::string path;
	uint32_t chunksize = 0;
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
		chunksize = 0;
	}
	SmbHandle(){}
	~SmbHandle(){ clear(); }
	int init(const char *server, const char *share, const char *user, const char *password, const char *path)
	{
		clear();
		smb = smb2_init_context();
		if (!smb)
			PELOG_ERROR_RETURN((PLV_ERROR, "RemoteSmb smb init failed\n"), Aresq::EINTERNAL);
		this->server = server;
		this->share = share;
		this->user = user;
		this->password = password;
		this->path = path;
		if (this->path == "/" || this->path == "." || this->path == "./")
			this->path == "";
		smb2_set_user(smb, this->user.c_str());
		smb2_set_password(smb, this->password.c_str());
		smb2_set_security_mode(smb, SMB2_NEGOTIATE_SIGNING_ENABLED);
		smb2_set_version(smb, SMB2_VERSION_ANY2);
		return Aresq::OK;
	}
	int connect()
	{
		if (connected)
			PELOG_ERROR_RETURN((PLV_WARNING, "Already connected smb\n"), Aresq::OK);
		int res = smb2_connect_share(smb, server.c_str(), share.c_str(), NULL);
		if (res == -EIO)
			PELOG_ERROR_RETURN((PLV_ERROR, "connect smb failed network %d\n", res), Aresq::DISCONNECTED);
		else if (res == -ECONNREFUSED || res == -ENOENT)
			PELOG_ERROR_RETURN((PLV_ERROR, "connect smb credential error %d\n", res), Aresq::EPARAM);
		else if (res < 0)
			PELOG_ERROR_RETURN((PLV_ERROR, "connect smb error %d\n", res), Aresq::EPARAM);
		connected = true;

		uint32_t maxchunksize = smb2_get_max_write_size(smb);
		chunksize = std::min(4 * 1024 * 1024u, maxchunksize);
		PELOG_LOG((PLV_VERBOSE, "maxchunksize smb %d, chunksize %d\n", maxchunksize, chunksize));
		return Aresq::OK;
	}
	void disconnect()
	{
		if (connected)
			smb2_disconnect_share(smb);
		connected = false;
	}
	operator smb2_context *() { return smb; }
	bool isconnected() const { return connected; }
	uint32_t getchunksize() const { return chunksize; }
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

std::string buildSmbPath(const char *root, const char *rbase, const char *path)
{
	std::string ret = path;
	if (rbase && *rbase)
		ret = std::string(rbase) + '/' + ret;
	if (root && *root)
		ret = std::string(root) + '/' + ret;
	return ret;
}

RemoteSmb::RemoteSmb()
{
	d = new RemoteSmbData;
}


RemoteSmb::~RemoteSmb()
{
	delete d;
	d = NULL;
}

Remote *RemoteSmb::fromConfig(const config_setting_t *config)
{
	const char *server, *share, *user, *password, *path;
	if (CONFIG_TRUE != config_setting_lookup_string(config, "server", &server))
		PELOG_ERROR_RETURN((PLV_ERROR, "RemoteSmb 'server' config not found\n"), NULL);
	if (CONFIG_TRUE != config_setting_lookup_string(config, "share", &share))
		PELOG_ERROR_RETURN((PLV_ERROR, "RemoteSmb 'share' config not found\n"), NULL);
	if (CONFIG_TRUE != config_setting_lookup_string(config, "user", &user))
		PELOG_ERROR_RETURN((PLV_ERROR, "RemoteSmb 'user' config not found\n"), NULL);
	if (CONFIG_TRUE != config_setting_lookup_string(config, "password", &password))
		PELOG_ERROR_RETURN((PLV_ERROR, "RemoteSmb 'password' config not found\n"), NULL);
	std::string passworddec = Aresq::decpwd(password);
	password = passworddec.c_str();
	if (CONFIG_TRUE != config_setting_lookup_string(config, "path", &path))
		PELOG_ERROR_RETURN((PLV_ERROR, "RemoteSmb 'path' config not found\n"), NULL);
	std::unique_ptr<RemoteSmb> ret(new RemoteSmb);
	if (ret->init(server, share, user, password, path) != Aresq::OK)
		return NULL;
	return ret.release();
}

int RemoteSmb::init(const char *server, const char *share, const char *user, const char *password, const char *path)
{
	int res = Aresq::OK;
	if ((res = d->smb.init(server, share, user, password, path)) != Aresq::OK)
		return res;

	if ((res = d->smb.connect()) != Aresq::OK)
		return res;

	return Aresq::OK;
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

struct SmbPutInfo
{
	int status = 0;
	smb2_context *smb = NULL;
	std::string name;
	FileHandle lfp;
	SmbFile rfp;
	uint64_t totalsize = 0;
	uint64_t writesize = 0;
	uint64_t realsize = 0;
	size_t chunksize = 0;
	size_t max_chunksize = 0;
	int *signal = NULL;
	abuf<char> buf;
};

inline size_t roundChunk(size_t size, size_t maxsize)
{
	if (maxsize >= 1024)
		return (size + (1024 - 1)) & ~((size_t)(1024 - 1));
	if (maxsize < 2)
		return size;
	while ((maxsize & (maxsize - 1)) != 0)
		maxsize = maxsize & (maxsize - 1);
	return (size + (maxsize - 1)) & ~((size_t)(maxsize - 1));
}

void onSmbPutChunk(struct smb2_context *smb2, int status, void *command_data, SmbPutInfo *info)
{
	if (status < 0)
	{
		info->status = status;
		PELOG_ERROR_RETURNVOID((PLV_ERROR, "Upload smb failed 4 %d\n", status));
	}
	if (status == 0 || status != info->chunksize)
		PELOG_ERROR_RETURNVOID((PLV_ERROR, "Upload smb failed 5\n"));
	info->writesize += status;
	PELOG_LOG((PLV_DEBUG, "smb put %d, %"PRIu64" / %"PRIu64" (%d%%). %s\n",
		status, info->writesize, info->totalsize,
		(int)(std::min(info->writesize, info->totalsize) * 100 / info->totalsize),
		info->name.c_str()));
	info->chunksize = fread(info->buf, 1, info->buf.size(), info->lfp);
	AuVerify(info->writesize == info->realsize || info->chunksize == 0);		// writesize != realsize only occurs when last chunk has just been put
	if (info->chunksize == 0)	// no more data
	{
		info->status = 1;
		return;
	}
	info->realsize += info->chunksize;
	info->chunksize = roundChunk(info->chunksize, info->max_chunksize);	// Some server may fail on certain chunksizes. write extra data and truncate as workaround
	int res = 0;
	if ((res = smb2_pwrite_async(info->smb, info->rfp, (const uint8_t *)info->buf.buf(), info->chunksize,
		info->writesize, (smb2_command_cb)onSmbPutChunk, info)) < 0)
	{
		info->status = res;
		PELOG_ERROR_RETURNVOID((PLV_ERROR, "Upload smb failed 6 %d\n", res));
	}
}


int RemoteSmb::smbPutFile(const char *lfile, const char *rfile)
{
	SmbPutInfo info;
	info.smb = d->smb;
	info.max_chunksize = d->smb.getchunksize();
	info.name = lfile;

	int res = 0;
	if (!(info.lfp = OpenFile(lfile, _NCT("rb"))))
		PELOG_ERROR_RETURN((PLV_ERROR, "Cannot read smb file %s\n", lfile), Aresq::FILELOCKED);
	fseek(info.lfp, 0, SEEK_END);
	info.totalsize = ftell(info.lfp);
	fseek(info.lfp, 0, SEEK_SET);
	info.rfp.setSmb(info.smb);
	if (!(info.rfp = smb2_open(info.smb, rfile, O_WRONLY | O_CREAT)))
	{
		// create file failed. try some house keeping
		const char *dirsep = strrchr(rfile, '/');
		if (!dirsep || addDir(std::string(rfile, dirsep)) < 0 || !(info.rfp = smb2_open(info.smb, rfile, O_WRONLY | O_CREAT)))
			PELOG_ERROR_RETURN((PLV_ERROR, "Cannot write smb remote file %s : %s \n", lfile, rfile), Aresq::EPARAM);
	}

	info.buf.resize(info.max_chunksize);
	info.chunksize = fread(info.buf, 1, info.buf.size(), info.lfp);
	info.realsize = info.chunksize;
	info.chunksize = roundChunk(info.chunksize, info.max_chunksize);	// Some server may fail on certain chunksizes. write extra data and the truncate as workaround
	if (info.realsize < info.chunksize)
		memset(info.buf + info.realsize, 0, info.chunksize - (size_t)info.realsize);
	if (info.chunksize == 0)
		info.status = 1;
	else if ((res = smb2_pwrite_async(d->smb, info.rfp, (const uint8_t *)info.buf.buf(), info.chunksize,
			info.writesize, (smb2_command_cb)onSmbPutChunk, &info)) < 0)
		PELOG_ERROR_RETURN((PLV_ERROR, "Upload smb failed 1 %d\n", res), Aresq::DISCONNECTED);

	while (info.status == 0)
	{
		pollfd pfd = { 0 };
		pfd.fd = smb2_get_fd(info.smb);
		pfd.events = smb2_which_events(info.smb);
		if ((res = poll(&pfd, 1, 1000)) < 0)
			PELOG_ERROR_RETURN((PLV_ERROR, "Upload smb failed 2 %d\n", res), Aresq::DISCONNECTED);
		if (pfd.revents == 0)
			continue;
		if ((res = smb2_service(info.smb, pfd.revents)) < 0)
			PELOG_ERROR_RETURN((PLV_ERROR, "Upload smb failed 3 %d: %s\n", res, smb2_get_error(info.smb)), Aresq::DISCONNECTED);
	}

	info.rfp.close();
	// always truncate, even if no extra data were written, in case of larger version of this file already exists
	if (info.status > 0 && (res = smb2_truncate(info.smb, rfile, info.realsize)) < 0)
		PELOG_ERROR_RETURN((PLV_ERROR, "Upload smb failed 7 %d: %s\n", res, smb2_get_error(info.smb)), Aresq::DISCONNECTED);

	if (info.status > 0)
		PELOG_ERROR_RETURN((PLV_VERBOSE, "PUTDONE smb %"PRIu64" %s -> %s\n", info.realsize, lfile, rfile), Aresq::OK);
	return Aresq::DISCONNECTED;
}

int RemoteSmb::getType(const char *fullpath)
{
	smb2_stat_64 stat;
	int res = smb2_stat(d->smb, fullpath, &stat);
	if (res == -ENOENT)	// not found
		return FT_NONE;
	if (res < 0)
		PELOG_ERROR_RETURN((PLV_ERROR, "smb stat failed %d: %s\n", res, fullpath), res);
	switch (stat.smb2_type)
	{
	case SMB2_TYPE_DIRECTORY:
		return FT_DIR;
	case SMB2_TYPE_FILE:
		return FT_FILE;
	case SMB2_TYPE_LINK:
		return FT_LINK;
	}
	return FT_UNK;
}

int RemoteSmb::addDir(const char *rbase, const char *path)
{
	std::string tpath = buildSmbPath(d->smb.path.c_str(), rbase, path);
	return addDir(tpath.c_str());
}

int RemoteSmb::addDir(const std::string &fullpath)
{
	if (!d->smb.isconnected())
		PELOG_ERROR_RETURN((PLV_ERROR, "ADDDIR smb remote disconnected: %s/%s\n", fullpath.c_str()), Aresq::DISCONNECTED);
	PELOG_LOG((PLV_DEBUG, "to ADDDIR smb %s\n", fullpath.c_str()));
	int res = smb2_mkdir(d->smb, fullpath.c_str());
	if (res == -EEXIST)
	{
		res = getType(fullpath.c_str());
		if (res < 0)
			PELOG_ERROR_RETURN((PLV_ERROR, "ADDDIR smb failed %d: %s\n", res, fullpath.c_str()), Aresq::EPARAM);
		if (res == FT_DIR)
			PELOG_ERROR_RETURN((PLV_VERBOSE, "Already exists smb: %s\n", fullpath.c_str()), Aresq::OK);
		PELOG_LOG((PLV_WARNING, "smb DIR name conflict. deleting the existing file. %s\n", fullpath.c_str()));
		if ((res = smb2_unlink(d->smb, fullpath.c_str())) == 0)
			res = smb2_mkdir(d->smb, fullpath.c_str());
	}
	else if (res == -ENOENT)
	{
		PELOG_LOG((PLV_VERBOSE, "Creating smb parent dir: %s\n", fullpath.c_str()));
		size_t sep = fullpath.rfind('/');
		if (sep == fullpath.npos)
			PELOG_ERROR_RETURN((PLV_ERROR, "ADDDIR smb create parent failed: %s\n", fullpath.c_str()), Aresq::EPARAM);
		size_t bsep = d->smb.path.empty() ? 0 : d->smb.path.length() + 1;
		if ((res = addDir(fullpath.substr(0, sep))) != Aresq::OK)
			PELOG_ERROR_RETURN((PLV_ERROR, "ADDDIR smb create parent failed: %s\n", fullpath.c_str()), Aresq::EPARAM);
		res = smb2_mkdir(d->smb, fullpath.c_str());
	}
	if (res != 0)
		PELOG_ERROR_RETURN((PLV_ERROR, "ADDDIR smb failed (%d: %s): %s\n", res, nterror_to_str(res), fullpath.c_str()), Aresq::EPARAM);
	PELOG_LOG((PLV_VERBOSE, "DIR smb added: %s\n", fullpath.c_str()));
	return Aresq::OK;
}

int RemoteSmb::addFile(const char *lbase, const char *rbase, const char *path)
{
	std::string lpath = std::string(lbase) + '/' + path;
	std::string rpath = buildSmbPath(d->smb.path.c_str(), rbase, path);
	return addFile(lpath, rpath);
}

int RemoteSmb::addFile(const std::string &lfullpath, const std::string &rfullpath)
{
	if (!d->smb.isconnected())
		PELOG_ERROR_RETURN((PLV_ERROR, "ADDFILE smb remote disconnected: %s\n", lfullpath.c_str()), Aresq::DISCONNECTED);
	int res = Aresq::OK;

	// put file to tmp dir
	char tmpbuf[32];
	{
		uint64_t timestamp =
			std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
#pragma warning(suppress : 4996)
		int pid = getpid();
		snprintf(tmpbuf, 32, "%" PRIu64 ".%d", timestamp, pid);
	}
	std::string tmpfn = buildSmbPath(d->smb.path.c_str(), AR_TMPDIR, tmpbuf);
	res = smbPutFile(lfullpath.c_str(), tmpfn.c_str());
	if (res != Aresq::OK)
		PELOG_ERROR_RETURN((PLV_ERROR, "ADDFILE smb failed 1:%d %s\n", res, lfullpath.c_str()), res);

	// move tmp file into dst file
	res = moveFile(tmpfn.c_str(), rfullpath.c_str(), true);
	if (res != 0)
		PELOG_ERROR_RETURN((PLV_ERROR, "ADDFILE smb failed 2:%d %s\n", res, lfullpath.c_str()), Aresq::EPARAM);
	PELOG_ERROR_RETURN((PLV_VERBOSE, "ADDFILE smb done 2 %s\n", rfullpath.c_str()), Aresq::OK);
}

int RemoteSmb::delDir(const char *rbase, const char *path)
{
	if (!path || !*path)
		PELOG_ERROR_RETURN((PLV_ERROR, "DELDIR invalid dir name\n"), Aresq::EPARAM);
	std::string tpath = buildSmbPath(d->smb.path.c_str(), rbase, path);
	return delDir(tpath);
}

int RemoteSmb::delDir(const std::string &fullpath)
{
	if (!d->smb.isconnected())
		PELOG_ERROR_RETURN((PLV_ERROR, "DELDIR remote disconnected: %s\n", fullpath.c_str()), Aresq::DISCONNECTED);

	// del contents
	int res = Aresq::OK;
	bool empty = false;
	SmbDir dir(d->smb);
	for (int retry = 0; retry < 3; ++retry)
	{
		dir = smb2_opendir(d->smb, fullpath.c_str());
		if (!dir)
			PELOG_ERROR_RETURN((PLV_WARNING, "smb remote dir not found\n"), Aresq::OK);
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
			res = item.second ? delDir(fullpath + item.first) : delFile(fullpath + item.first);
			if (res != Aresq::OK)
				return res;
		}
	}

	if (!empty)
		PELOG_ERROR_RETURN((PLV_ERROR, "DELDIR: del contents failed %s\n", fullpath.c_str()), Aresq::EINTERNAL);

	PELOG_LOG((PLV_DEBUG, "to DELDIR smb %s\n", fullpath.c_str()));
	res = smb2_rmdir(d->smb, fullpath.c_str());
	if (res < 0 && res != -ENOENT)
		PELOG_ERROR_RETURN((PLV_ERROR, "DELDIR smb failed %d: %s\n", res, fullpath.c_str()), Aresq::EPARAM);

	// verify
	res = isDir(fullpath.c_str());
	if (res != 0)
		PELOG_ERROR_RETURN((PLV_ERROR, "DELDIR smb failed %d %s\n", res, fullpath.c_str()), Aresq::EPARAM);

	PELOG_ERROR_RETURN((PLV_VERBOSE, "DELDIR smb done\n"), Aresq::OK);
}

int RemoteSmb::delFile(const char *rbase, const char *path)
{
	std::string tpath = buildSmbPath(d->smb.path.c_str(), rbase, path);
	return delFile(tpath);
}

int RemoteSmb::delFile(const std::string &fullpath)
{
	if (!d->smb.isconnected())
		PELOG_ERROR_RETURN((PLV_ERROR, "DELFILE smb remote disconnected: %s\n", fullpath), Aresq::DISCONNECTED);
	int res = smb2_unlink(d->smb, fullpath.c_str());
	if (res < 0 && res != -ENOENT)
		PELOG_ERROR_RETURN((PLV_ERROR, "DELFILE smb failed %d: %s\n", res, fullpath.c_str()), Aresq::EPARAM);
	PELOG_ERROR_RETURN((PLV_VERBOSE, "DELFILE smb done\n"), Aresq::OK);
}

int RemoteSmb::putHist(const char *rbase, const char *path)
{
	std::string rpath = buildSmbPath(d->smb.path.c_str(), rbase, path);
	std::string histpath = buildSmbPath(d->smb.path.c_str(), AR_HISTDIR, rbase) + '/' + path;
	uint64_t timestamp =
		std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
	char tmpbuf[32];
	snprintf(tmpbuf, 32, ".%" PRIu64, timestamp);
	histpath += tmpbuf;
	int res = moveFile(rpath.c_str(), histpath.c_str(), true);
	if (res == Aresq::NOTFOUND)
		PELOG_ERROR_RETURN((PLV_WARNING, "HIST smb src not exist %s\n", rpath.c_str()), Aresq::OK);
	else if (res != Aresq::OK)
		PELOG_ERROR_RETURN((PLV_ERROR, "HIST smb failed %d %s\n", res, rpath.c_str()), Aresq::EPARAM);
	PELOG_ERROR_RETURN((PLV_VERBOSE, "HIST smb done %s\n", histpath.c_str()), Aresq::OK);
}

int RemoteSmb::moveFile(const char *oldpath, const char *newpath, bool force)
{
	int res = Aresq::OK;
	// move tmp file into dst file
	res = smb2_rename(d->smb, oldpath, newpath);
	if (res == 0)
		PELOG_ERROR_RETURN((PLV_VERBOSE, "MOVEFILE smb done 1\n"), Aresq::OK);

	// move failed, try some house keeping
	if (getType(oldpath) == FT_NONE)
		PELOG_ERROR_RETURN((PLV_ERROR, "MOVEFILE src not exist %s\n", oldpath), Aresq::NOTFOUND);
	if (force)
	{
		// delete dst item
		int type = getType(newpath);
		if (type == FT_DIR)
			delDir(newpath);
		else if (type == FT_FILE || type == FT_LINK)
			delFile(newpath);
	}
	// create parent dir
	const char *pathsep = strrchr(newpath, '/');
	if (pathsep)
	{
		std::string parentpath(newpath, pathsep);
		res = addDir(parentpath);
		if (res != Aresq::OK)
			PELOG_ERROR_RETURN((PLV_ERROR, "MOVEFILE smb parent failed %d %s\n", res, newpath), res);
	}

	// try move again
	res = smb2_rename(d->smb, oldpath, newpath);
	if (res != 0)
		PELOG_ERROR_RETURN((PLV_ERROR, "MOVEFILE smb failed 2:%d %s\n", res, oldpath), Aresq::EPARAM);
	PELOG_ERROR_RETURN((PLV_VERBOSE, "MOVEFILE smb done 2 %s\n", newpath), Aresq::OK);
}