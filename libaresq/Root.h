#pragma once

#include <vector>
#include <deque>
#include <map>
#include "record.h"
#include "auto_buf.hpp"
#include "fsadapter.h"
#include "Remote.h"
#include "AresqIgnore.h"

class Root
{
public:
	Root();
	~Root();

	// back up contents of `root` into remote/`name`, using `recpath` as local registry
	int load(int id, const char *name, const char *root, const char *rec_path, AresqIgnore *aresqignore);

	struct Action
	{
		enum { NONE, ADDDIR, DELDIR, ADDFILE, DELFILE, MODFILE, RENAME} type = NONE;
		abufchar name;
		abufchar dst;
		//union
		//{
		//	struct AddParam
		//	{
		//		bool ignore;
		//		bool onlylocal;
		//	} add;
		//	struct DelParam
		//	{
		//		bool recycle;
		//	} del;
		//} param = { 0 };
	};
	int startRefresh();
	// return: 0: finished, >0: one step, <0: error
	int refreshStep(int state, Action &action);
	int perform(Action &action, Remote *remote);
	//int addDir(const char *dir) { uint32_t did = 0;  return addDir(dir, strlen(dir), did); }
	int addFile(const char *file, Remote *remote) { uint32_t fid = 0;  return addFile(file, strlen(file), fid, remote); }

	int addDir(const char *dir, size_t dlen, uint32_t &did, Remote *remote);
	int delDir(const char *dir, size_t dlen, Remote *remote);
	int delDir(uint32_t rid, uint32_t pid, const char *dir, size_t dlen, Remote *remote);
	int addFile(const char *file, size_t flen, uint32_t &fid, Remote *remote);
	int delFile(const char *filename, size_t flen, Remote *remote);
	int delFile(uint32_t rid, uint32_t pid, const char *filename, size_t flen, Remote *remote);
	int eraseName(uint32_t rid);

	// look for the specific name under pid, return rid if found, otherwise pre or parent id
	// FindResult indicates whether the returned record id is matched or pre item
	enum FindResult { FR_MATCH, FR_PRE, FR_PARENT, FR_NONE };
	uint32_t findRecord(uint32_t pid, const char *name, size_t namelen, FindResult &restype);
	// look in whole root, parent id will be returned in `pid`
	uint32_t findRecordRoot(const char *name, size_t namelen, FindResult &restype, uint32_t &pid);

private:
	// configs
	int rootid = -1;	// id of this root
	std::string _name;	// name of this root. all files will be backed-up in <remote>/name dir
	std::string _localroot;	// the dir in local storage to backup
	//abuf<char> ncroot;
	std::string recpath;	// the dir used as local registry

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
		uint32_t prog = 0;
		std::vector<FsItem> files;
	};
	std::deque<RefreshIter> restate;
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
	struct RootStat
	{
		uint32_t nfile = 0;
		uint32_t ndir = 0;
		uint32_t nrecy = 0;
	};
	bool verifyrec(RootStat *stat=NULL) const;
	bool verifydir(uint32_t pid) const;

	inline const char *getName(uint32_t rid) const { AuVerify(rid > 1 && rid < _records.size()); return _records[rid].name(_rname); }
	inline const char *getName(const RecordItem &rec) const { return rec.name(_rname); }

	// records operations
	uint32_t allocRName(const char *name, size_t len);	// alloc string in _rname, and write to disk
	uint32_t allocRec(std::vector<uint32_t> &cids);		// alloc a new item in _record. change in memory only, use writeRec() to write to disk
	int recycleRec(uint32_t rid, std::vector<uint32_t> &cids);
	int writeRec(std::vector<uint32_t> &cids);	// write back records to file
};

