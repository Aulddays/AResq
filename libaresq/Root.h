#pragma once

#include <vector>
#include <deque>
#include <map>
#include "record.h"
#include "auto_buf.hpp"
#include "fsadapter.h"
#include "Remote.h"

class Root
{
public:
	enum
	{
		OK = 0,
		DISCONNECTED = -1,
		CONFLICT = -2,
		REMOTEERR = -3,
		NOTFOUND = -4,
		NOTIMPLEMENTED = -5,
	};
public:
	Root(const char *recpath, const char *root);
	~Root();

	int load();

	struct Action
	{
		enum { NONE, ADDDIR, DELDIR, ADDFILE, DELFILE, RENAME} type = NONE;
		abufchar name;
		abufchar dst;
	};
	int startRefresh();
	// return: 0: finished, >0: one step, <0: error
	int refreshStep(int state, Action &action);
	int perform(Action &action);
	//int addDir(const char *dir) { uint32_t did = 0;  return addDir(dir, strlen(dir), did); }
	int addFile(const char *file) { uint32_t fid = 0;  return addFile(file, strlen(file), fid); }

	int addDir(const char *dir, size_t dlen, uint32_t &did);
	int delDir(const char *dir, size_t dlen);
	int delDir(uint32_t rid, uint32_t pid, const char *dir, size_t dlen);
	int addFile(const char *file, size_t flen, uint32_t &fid);
	int delFile(const char *filename, size_t flen);
	int delFile(uint32_t rid, uint32_t pid, const char *filename, size_t flen);
	int eraseName(uint32_t rid);

	// look for the specific name under pid
	// FindOptions specifies which type of item to find
	// FO_FILE: !isdel file or dir, otherwise isdel file, otherwise pre
	// FO_FILEDEL: isdel or !isdel file, otherwise 0
	// FO_DIR: !isdel file or dir, otherwise pre
	// FO_DIRDEL: isdel dir, otherwise 0
	// FindResult indicates whether the returned record id is matched or pre item
	enum FindOption { FO_FILE, FO_FILEDEL, FO_DIR, FO_DIRDEL };
	enum FindResult { FR_MATCH, FR_PRE };
	uint32_t findRecord(uint32_t pid, const char *name, size_t namelen, FindOption option, FindResult &restype);
	// look in whole root, parent id will be in `pid`
	uint32_t findRecord(const char *name, size_t namelen, FindOption option, FindResult &restype, uint32_t &pid);

private:
	std::string root;
	//abuf<char> ncroot;
	std::string recpath;
	size_t _histnum = 0;
	std::vector<RecordItem> _records;
	std::vector<char> _rname;
	std::vector<HistItem> _hists;

	Remote *_remote = NULL;

	struct RefreshIter
	{
		uint32_t rid = 0;
		abufchar name;
		abufchar path;	// full path
		enum
		{
			INIT,
			DELFILE,
			DELDIR,
			FILE,
			DIR,
			RECUR,
			REDOUPPER,
			RETURN,
		} stage = INIT;
		uint32_t prog = 0;
		std::vector<FsItem> files;
		std::vector<FsItem> dirs;
	};
	std::deque<RefreshIter> restate;
	std::map<uint32_t, int> redostate;	// record redo during refresh, for debugging
	//int refreshBuildPath(abuf<char> &path);

	enum RecPtrType { RPNONE, RPSUB, RPNEXT };
	struct RecPtr
	{
		uint32_t _id = 0;
		RecPtrType _type = RPNONE;
		inline void set(uint32_t id, RecPtrType type) { _id = id; _type = type; AuVerify(type != RPNONE); }
		inline uint32_t operator()(Root *root)
		{
			AuVerify(_id != 0 && (_type == RPSUB || _type == RPNEXT));
			if (_type == RPSUB)
				return root->_records[_id].sub();
			else
				return root->_records[_id].next();
		}
		inline void operator()(uint32_t tid, Root *root)
		{
			AuVerify(_id != 0 && (_type == RPSUB || _type == RPNEXT));
			if (_type == RPSUB)
				root->_records[_id].sub(tid);
			else
				root->_records[_id].next(tid);
		}
	};

	int init();
	struct RootStat
	{
		uint32_t nfile = 0;
		uint32_t ndir = 0;
		uint32_t ndelf = 0;
		uint32_t ndeld = 0;
		uint32_t nrecy = 0;
	};
	bool verifyrec(RootStat *stat=NULL) const;
	bool verifydir(uint32_t pid) const;

	inline const char *getName(uint32_t rid) const { AuVerify(rid > 1 && rid < _records.size()); return _records[rid].name(_rname); }

	// records operations
	uint32_t allocRName(const char *name, size_t len);	// alloc string in _rname, and write to disk
	uint32_t allocRec(std::vector<uint32_t> &cids);		// alloc a new item in _record. change in memory only, use writeRec() to write to disk
	int recycleRec(uint32_t rid, std::vector<uint32_t> &cids);
	int writeRec(std::vector<uint32_t> &cids);	// write back records to file
};

