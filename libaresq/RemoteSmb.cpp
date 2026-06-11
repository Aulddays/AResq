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
#include <functional>
#include <chrono>
#include <process.h>

#include "Aresq.h"
#include "auto_buf.hpp"
#include "fsadapter.h"
#include "libsmb2/include/smb2.h"
#include "libsmb2/include/libsmb2.h"
#ifdef _MSC_VER
#	define snprintf _snprintf
#	define getpid _getpid
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
		smb2_set_timeout(smb, 120);
		return Aresq::OK;
	}
	int connect()
	{
		if (connected)
		{
			PELOG_LOG((PLV_WARNING, "Already connected smb. re-connect.\n"));
			disconnect();
		}
		int res = smb2_connect_share(smb, server.c_str(), share.c_str(), NULL);
		if (res == -EIO)
			PELOG_ERROR_RETURN((PLV_ERROR, "connect smb failed network %d\n", res), Aresq::DISCONNECTED);
		else if (res == -ECONNREFUSED || res == -ENOENT)
			PELOG_ERROR_RETURN((PLV_ERROR, "connect smb credential error %d\n", res), Aresq::EPARAM);
		else if (res < 0)
			PELOG_ERROR_RETURN((PLV_ERROR, "connect smb error %d\n", res), Aresq::EPARAM);
		connected = true;

		uint32_t maxchunksize = smb2_get_max_write_size(smb);
		chunksize = std::min(1 * 1024 * 1024u, maxchunksize);
		PELOG_LOG((PLV_INFO, "Connected smb. maxchunksize %d, chunksize %d\n", maxchunksize, chunksize));
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

std::unique_ptr<Remote> RemoteSmb::fromConfig(const config_setting_t *config)
{
	const char *server, *share, *user, *password, *path;
	if (CONFIG_TRUE != config_setting_lookup_string(config, "server", &server))
		PELOG_ERROR_RETURN((PLV_ERROR, "RemoteSmb 'server' config not found\n"), nullptr);
	if (CONFIG_TRUE != config_setting_lookup_string(config, "share", &share))
		PELOG_ERROR_RETURN((PLV_ERROR, "RemoteSmb 'share' config not found\n"), nullptr);
	if (CONFIG_TRUE != config_setting_lookup_string(config, "user", &user))
		PELOG_ERROR_RETURN((PLV_ERROR, "RemoteSmb 'user' config not found\n"), nullptr);
	if (CONFIG_TRUE != config_setting_lookup_string(config, "password", &password))
		PELOG_ERROR_RETURN((PLV_ERROR, "RemoteSmb 'password' config not found\n"), nullptr);
	std::string passworddec = Aresq::decpwd(password);
	password = passworddec.c_str();
	if (CONFIG_TRUE != config_setting_lookup_string(config, "path", &path))
		PELOG_ERROR_RETURN((PLV_ERROR, "RemoteSmb 'path' config not found\n"), nullptr);
	std::unique_ptr<RemoteSmb> ret(new RemoteSmb);
	if (ret->init(server, share, user, password, path) != Aresq::OK)
		return nullptr;
	return std::move(ret);
}

int RemoteSmb::init(const char *server, const char *share, const char *user, const char *password, const char *path)
{
	std::lock_guard<std::mutex> lock(mutex);
	return d->smb.init(server, share, user, password, path);
}

int RemoteSmb::connect()
{
	std::lock_guard<std::mutex> lock(mutex);
	return d->smb.connect();
}

void RemoteSmb::disconnect()
{
	std::lock_guard<std::mutex> lock(mutex);
	d->smb.disconnect();
}

bool RemoteSmb::isConnected() const
{
	std::lock_guard<std::mutex> lock(mutex);
	return d->smb.isconnected();
}

int RemoteSmb::smbPutFileNolock(const char *lfile, const char *rfile)
{
	uint64_t ftime = 0;
	uint64_t totalsize = 0;
	bool isdir_dummy = false;
	if (getFileAttr("", lfile, strlen(lfile), ftime, totalsize, isdir_dummy) != 0)
		PELOG_ERROR_RETURN((PLV_ERROR, "Cannot access %s\n", lfile), Aresq::INACCESIBLE);

	FileHandle lfp = OpenFile(lfile, _NCT("rb"));	// open local
	if (!lfp)
		PELOG_ERROR_RETURN((PLV_ERROR, "Cannot read local file %s\n", lfile), Aresq::FILELOCKED);

	std::unique_ptr<smb2fh, std::function<void(smb2fh *)>> rfp {	// open remote
		smb2_open(d->smb, rfile, O_WRONLY | O_CREAT),
		[this](smb2fh *fp) { smb2_close(d->smb, fp); } };	// auto close smb file handle using unique_ptr
	if (!rfp)
	{
		// create file failed. try some house keeping
		const char *dirsep = strrchr(rfile, '/');
		if (!dirsep || addDirNolock(std::string(rfile, dirsep)) < 0 || !(rfp.reset(smb2_open(d->smb, rfile, O_WRONLY | O_CREAT)), rfp))
			PELOG_ERROR_RETURN((PLV_ERROR, "Cannot write smb remote file %s : %s \n", lfile, rfile), Aresq::EPARAM);
	}

	// write content
	uint32_t chunksize = d->smb.getchunksize();
	std::unique_ptr<uint8_t> buf(new uint8_t[chunksize]);
	uint64_t readsize = 0, donesize = 0;
	while ((readsize = fread(buf.get(), 1, chunksize, lfp)) > 0)
	{
		int res = smb2_write(d->smb, rfp.get(), buf.get(), (uint32_t)readsize);
		if (res < 0)
			PELOG_ERROR_RETURN((PLV_ERROR, "Upload smb failed (%" PRIu64 ":%" PRIu64 ") %d\n",
				donesize, totalsize, res), Aresq::DISCONNECTED);
		donesize += res;
		PELOG_LOG((PLV_DEBUG, "smb putchunk %d, %" PRIu64 " / %" PRIu64 " (%d%%). %s\n",
			res, donesize, totalsize, (int)(std::min(donesize, totalsize) * 100 / totalsize), lfile));
	}
	if (donesize != totalsize)
		PELOG_LOG((PLV_WARNING, "smb put size mismatch %" PRIu64 ":%" PRIu64 "\n", donesize, totalsize));

	// set file time
	smb2_timeval ftimeval { (time_t)ftime, 0 };
	smb2_futimes(d->smb, rfp.get(), NULL, NULL, &ftimeval, NULL);

	rfp.reset();
	PELOG_LOG((PLV_VERBOSE, "PUTDONE smb %" PRIu64 " %s -> %s\n", totalsize, lfile, rfile));
	return Aresq::OK;
}

int RemoteSmb::getType(const char *rbase, const char *path)
{
	std::lock_guard<std::mutex> lock(mutex);
	std::string tpath = buildSmbPath(d->smb.path.c_str(), rbase, path);
	return getTypeNolock(tpath.c_str());
}

int RemoteSmb::getTypeNolock(const char *fullpath)
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
	std::lock_guard<std::mutex> lock(mutex);
	std::string tpath = buildSmbPath(d->smb.path.c_str(), rbase, path);
	return addDirNolock(tpath.c_str());
}

int RemoteSmb::addDirNolock(const std::string &fullpath)
{
	if (!d->smb.isconnected())
		PELOG_ERROR_RETURN((PLV_ERROR, "ADDDIR smb remote disconnected: %s/%s\n", fullpath.c_str()), Aresq::DISCONNECTED);
	PELOG_LOG((PLV_DEBUG, "to ADDDIR smb %s\n", fullpath.c_str()));
	int res = smb2_mkdir(d->smb, fullpath.c_str());
	if (res == -EEXIST)
	{
		res = getTypeNolock(fullpath.c_str());
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
		if ((res = addDirNolock(fullpath.substr(0, sep))) != Aresq::OK)
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
	std::lock_guard<std::mutex> lock(mutex);
	std::string lpath = std::string(lbase) + '/' + path;
	std::string rpath = buildSmbPath(d->smb.path.c_str(), rbase, path);
	return addFileNolock(lpath, rpath);
}

int RemoteSmb::addFileNolock(const std::string &lfullpath, const std::string &rfullpath)
{
	if (!d->smb.isconnected())
		PELOG_ERROR_RETURN((PLV_ERROR, "ADDFILE smb remote disconnected: %s\n", lfullpath.c_str()), Aresq::DISCONNECTED);
	int res = Aresq::OK;

	// put file to tmp dir
	char tmpbuf[32];
	{
		uint64_t timestamp =
			std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
		int pid = getpid();
		snprintf(tmpbuf, 32, "%" PRIu64 ".%d", timestamp, pid);
	}
	std::string tmpfn = buildSmbPath(d->smb.path.c_str(), AR_TMPDIR, tmpbuf);
	res = smbPutFileNolock(lfullpath.c_str(), tmpfn.c_str());
	if (res != Aresq::OK)
		PELOG_ERROR_RETURN((PLV_ERROR, "ADDFILE smb failed 1:%d %s\n", res, lfullpath.c_str()), res);

	// move tmp file into dst file
	res = moveFileNolock(tmpfn, rfullpath, true);
	if (res != 0)
		PELOG_ERROR_RETURN((PLV_ERROR, "ADDFILE smb failed 2:%d %s\n", res, lfullpath.c_str()), Aresq::EPARAM);
	PELOG_ERROR_RETURN((PLV_VERBOSE, "ADDFILE smb done 2 %s\n", rfullpath.c_str()), Aresq::OK);
}

int RemoteSmb::delDir(const char *rbase, const char *path)
{
	std::lock_guard<std::mutex> lock(mutex);
	if (!path || !*path)
		PELOG_ERROR_RETURN((PLV_ERROR, "DELDIR invalid dir name\n"), Aresq::EPARAM);
	std::string tpath = buildSmbPath(d->smb.path.c_str(), rbase, path);
	return delDirNolock(tpath);
}

int RemoteSmb::delDirNolock(const std::string &fullpath)
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
			res = item.second ? delDirNolock(fullpath + item.first) : delFileNolock(fullpath + item.first);
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
	res = isDirNolock(fullpath.c_str());
	if (res != 0)
		PELOG_ERROR_RETURN((PLV_ERROR, "DELDIR smb failed %d %s\n", res, fullpath.c_str()), Aresq::EPARAM);

	PELOG_ERROR_RETURN((PLV_VERBOSE, "DELDIR smb done\n"), Aresq::OK);
}

int RemoteSmb::delFile(const char *rbase, const char *path)
{
	std::lock_guard<std::mutex> lock(mutex);
	std::string tpath = buildSmbPath(d->smb.path.c_str(), rbase, path);
	return delFileNolock(tpath);
}

int RemoteSmb::delFileNolock(const std::string &fullpath)
{
	if (!d->smb.isconnected())
		PELOG_ERROR_RETURN((PLV_ERROR, "DELFILE smb remote disconnected: %s\n", fullpath.c_str()), Aresq::DISCONNECTED);
	int res = smb2_unlink(d->smb, fullpath.c_str());
	if (res < 0 && res != -ENOENT)
		PELOG_ERROR_RETURN((PLV_ERROR, "DELFILE smb failed %d: %s\n", res, fullpath.c_str()), Aresq::EPARAM);
	PELOG_ERROR_RETURN((PLV_VERBOSE, "DELFILE smb done\n"), Aresq::OK);
}

int RemoteSmb::putHist(const char *rbase, const char *path)
{
	std::lock_guard<std::mutex> lock(mutex);
	std::string rpath = buildSmbPath(d->smb.path.c_str(), rbase, path);
	std::string histpath = buildSmbPath(d->smb.path.c_str(), AR_HISTDIR, rbase) + '/' + path;
	uint64_t timestamp =
		std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
	char tmpbuf[32];
	snprintf(tmpbuf, 32, ".%" PRIu64, timestamp);
	histpath += tmpbuf;
	int res = moveFileNolock(rpath, histpath, true);
	if (res == Aresq::NOTFOUND)
		PELOG_ERROR_RETURN((PLV_WARNING, "HIST smb src not exist %s\n", rpath.c_str()), Aresq::OK);
	else if (res != Aresq::OK)
		PELOG_ERROR_RETURN((PLV_ERROR, "HIST smb failed %d %s\n", res, rpath.c_str()), Aresq::EPARAM);
	PELOG_ERROR_RETURN((PLV_INFO, "HIST smb done %s\n", histpath.c_str()), Aresq::OK);
}

int RemoteSmb::moveFileNolock(const std::string &fullsrcpath, const std::string &fulldstpath, bool force)
{
	int res = Aresq::OK;
	// move src file into dst file
	res = smb2_rename(d->smb, fullsrcpath.c_str(), fulldstpath.c_str());
	if (res == 0)
		PELOG_ERROR_RETURN((PLV_VERBOSE, "MOVEFILE smb done 1\n"), Aresq::OK);

	// move failed, try some house keeping
	if (getTypeNolock(fullsrcpath.c_str()) == FT_NONE)
		PELOG_ERROR_RETURN((PLV_ERROR, "MOVEFILE src not exist %s\n", fullsrcpath.c_str()), Aresq::NOTFOUND);
	if (force)
	{
		// delete dst item
		int type = getTypeNolock(fulldstpath.c_str());
		if (type == FT_DIR)
			delDirNolock(fulldstpath.c_str());
		else if (type == FT_FILE || type == FT_LINK)
			delFileNolock(fulldstpath.c_str());
	}
	// create parent dir
	const char *pathsep = strrchr(fulldstpath.c_str(), '/');
	if (pathsep)
	{
		std::string parentpath(fulldstpath.c_str(), pathsep);
		res = addDirNolock(parentpath);
		if (res != Aresq::OK)
			PELOG_ERROR_RETURN((PLV_ERROR, "MOVEFILE smb parent failed %d %s\n", res, fulldstpath.c_str()), res);
	}

	// try move again
	res = smb2_rename(d->smb, fullsrcpath.c_str(), fulldstpath.c_str());
	if (res != 0)
		PELOG_ERROR_RETURN((PLV_ERROR, "MOVEFILE smb failed 2:%d %s\n", res, fulldstpath.c_str()), Aresq::EPARAM);
	PELOG_ERROR_RETURN((PLV_VERBOSE, "MOVEFILE smb done 2 %s\n", fulldstpath.c_str()), Aresq::OK);
}

int RemoteSmb::moveFile(const char *rbase, const char *srcpath, const char *dstpath, bool force)
{
	std::lock_guard<std::mutex> lock(mutex);
	std::string srcfull = buildSmbPath(d->smb.path.c_str(), rbase, srcpath);
	std::string dstfull = buildSmbPath(d->smb.path.c_str(), rbase, dstpath);
	return moveFileNolock(srcfull, dstfull, force);
}
