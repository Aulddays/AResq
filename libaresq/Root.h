#pragma once

#include <vector>
#include <deque>
#include <map>
#include <mutex>
#include "record.h"
#include "auto_buf.hpp"
#include "fsadapter.h"
#include "Remote.h"
#include "AresqIgnore.h"

class Root
{
	// IMPORTANT: all public functions MUST acquire _mutex
public:
	Root();
	~Root();

	// contents of `root` will be backed up into remote/`name`, using `recpath` as local registry
	int load(int id, const char *name, const char *root, const char *rec_path, bool keephist, AresqIgnore *aresqignore);
	bool loaded () const { return rootid != -1; }

	struct Action
	{
		enum { NONE, BREAK, ADDDIR, DELDIR, ADDFILE, DELFILE, MODFILE, RENAME} type = NONE;
		abufchar name;
		abufchar dst;
		bool isignore = false;
		bool keephist = false;
	};

	// Preparing restate for complete refresh, optional initstep for restore previous progress
	int startRefresh(const std::vector<std::string> *initstep, std::unique_lock<std::mutex> &refreshLock);

	// Preparing restate for refreshing single path, optionally recuring into sub dirs
	// return: OK: finished by simple update, AGAIN: continue with refreshStep, otherwise error
	int startRefreshSingle(const char *path, Remote *remote, bool recur, std::unique_lock<std::mutex> &refreshLock);

	// return: OK: finished, AGAIN: one step, other: error
	int refreshStep(int state, Action &action);
	int refreshSave(std::vector<std::string> *step);
	int perform(Action &action, Remote *remote);

	uint32_t getRecordTime(const char *path) const;
	int getRecordType(const char *path) const;	// -1: not found, 0: file, 1: dir

	// compact the data records by reclaim deleted content in names
	int compact(uint32_t limit=0);

	bool verify() const { std::lock_guard<std::mutex> lock(_mutex); return verifyrec(_records, _rname); }

private:
	mutable std::mutex _mutex;
	std::mutex _refreshMutex;

	// configs
	int rootid = -1;	// id of this root
	std::string _name;	// name of this root. all files will be backed-up in <remote>/name dir
	std::string _localroot;	// the dir in local storage to backup
	//abuf<char> ncroot;
	std::string recpath;	// the dir used as local registry

	bool keephist;

	AresqIgnore *ignore;

	// local registry data
	// _records format:
	// _records[0] is reserved for the head of recycled items.
	//     _records[0].next() -> next recycled item, until next() == 0
	// each non-recycled record:
	//     rec.name() -> pos of name in _rname
	//     rec.next() -> next sibling item inside parent dir. if last item, -> parent
	//     rec.time(): last modified time, 32 bit
	// _records[1] is the root dir item
	// dir record:
	//     rec.sub() -> first child inside this dir.
	//         rec.sub() == 0 if the dir is empty
	//     order of items inside the same dir: name in case-insensitive C order
	//     if a dir is not empty rec.next() of the last item points back to parent
	// file record:
	//     size(): file size, lower 3-bytes only
	std::vector<RecordItem> _records;
	std::vector<char> _rname;

	//Remote *_remote = NULL;

	struct RefreshIter
	{
		uint32_t rid = 0;
		abufchar name;
		abufchar path;	// full abs path
		enum
		{
			INIT,
			REMOVE,
			NEW,
			RECUR,
			REDOUPPER,
			RETURN,
		} stage = INIT;
		enum
		{
			NOUPPER = 1 << 0,	// do not fallback to parent on REDOUPPER / NOTFOUND
			NORECUR = 1 << 1,	// finish after current dir; do not enter child dirs
		};
		uint32_t iterConfig = 0;
		bool noupper() const { return (iterConfig & NOUPPER) != 0; }
		bool norecur() const { return (iterConfig & NORECUR) != 0; }
		uint32_t prog = 0;
		std::vector<FsItem> files;
	};
	std::vector<RefreshIter> restate;	// current refresh status
	std::vector<std::string> reinit;	// saved step of last unfinished refresh, to start with
	std::map<std::string, int> failstate;	// record fail during refresh, for debugging
	bool recordFail(const char *path)
	{
		failstate[path]++;
		if (failstate[path] > 2)
		{
			PELOG_LOG((PLV_ERROR, "Too many fails. %s\n", path));
			pelog_flush();
			return false;
		}
		return true;
	}
	//int refreshBuildPath(abuf<char> &path);

	enum RecPtrType { RPNONE, RPSUB, RPNEXT };
	struct RecPtr
	{
		RecPtr(Root *root): _root(root) {}
		uint32_t _id = 0;
		RecPtrType _type = RPNONE;
		Root *_root = NULL;
		inline void set(uint32_t id, RecPtrType type) { _id = id; _type = type; AuVerify(type != RPNONE); }
		inline uint32_t operator()()
		{
			AuVerify(_id != 0 && (_type == RPSUB || _type == RPNEXT));
			if (_type == RPSUB)
				return _root->_records[_id].sub();
			else
				return _root->_records[_id].next();
		}
		inline void operator()(uint32_t tid)
		{
			AuVerify(_id != 0 && (_type == RPSUB || _type == RPNEXT));
			if (_type == RPSUB)
				_root->_records[_id].sub(tid);
			else
				_root->_records[_id].next(tid);
		}
	};

	int init();

	// detect and cleanup any failed compact transaction
	int recoverCompact();

	int addDir(const char *dir, size_t dlen, bool isignore, uint32_t &did, Remote *remote);
	int delDir(const char *dir, size_t dlen, bool isignore, bool keephist, bool noremote, Remote *remote);
	int delDir(uint32_t rid, uint32_t pid, const char *dir, size_t dlen, bool isignore, bool keephist, bool noremote, Remote *remote);
	int addFile(const char *file, size_t flen, bool isignore, bool keephist, uint32_t &fid, Remote *remote);
	int delFile(const char *filename, size_t flen, bool isignore, bool keephist, bool noremote, Remote *remote);
	int delFile(uint32_t rid, uint32_t pid, const char *filename, size_t flen, bool isignore, bool keephist, bool noremote, Remote *remote);
	int rename(const char *src, const char *dst, Remote *remote);
	int eraseName(uint32_t rid);

	// look for the specific name under pid, return rid if found, otherwise pre or parent id
	// FindResult indicates whether the returned record id is matched or pre item
	enum FindResult { FR_MATCH, FR_PRE, FR_PARENT, FR_NONE };
	uint32_t findRecord(uint32_t pid, const char *name, size_t namelen, FindResult &restype) const;
	// look in whole root, parent id will be returned in `pid`
	uint32_t findRecordRoot(const char *name, size_t namelen, FindResult &restype, uint32_t &pid) const;

	struct RootStat
	{
		uint32_t nfile = 0;
		uint32_t ndir = 0;
		uint32_t nrecy = 0;
	};
	bool verifyrec(const std::vector<RecordItem> &records, const std::vector<char> &rname, RootStat *stat=NULL) const;
	bool verifydir(uint32_t pid) const;

	inline const char *getName(uint32_t rid) const { AuVerify(rid > 1 && rid < _records.size()); return _records[rid].name(_rname); }
	inline const char *getName(const RecordItem &rec) const { return rec.name(_rname); }

	// records operations
	uint32_t allocRName(const char *name, uint32_t len);	// alloc string in _rname, and write to disk
	uint32_t allocRec(std::vector<uint32_t> &cids);		// alloc a new item in _record. change in memory only, use writeRec() to write to disk
	int recycleRec(uint32_t rid, std::vector<uint32_t> &cids);
	int writeRec(std::vector<uint32_t> &cids);	// write back records to file
};

