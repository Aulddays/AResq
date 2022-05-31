#include "stdafx.h"
#include "Root.h"
#include <stack>
#include <algorithm>
#include <map>
#include "fsadapter.h"
#include "utfconv.h"
#include "pe_log.h"


Root::Root(const char *recpath, const char *root):
	recpath(recpath), root(root)
{

}

Root::~Root()
{
	delete _remote;
}

static size_t getFileSize(FILE *fp)
{
	size_t pos = ftell(fp);
	fseek(fp, 0, SEEK_END);
	size_t size = ftell(fp);
	fseek(fp, pos, SEEK_SET);
	return size;
}

int Root::load()
{
	if (CreateDir(recpath.c_str()) != 0)
		PELOG_ERROR_RETURN((PLV_ERROR, "Create RECPATH failed\n"), -1);

	FILE *fp = NULL;
	// load record
	if (!(fp = OpenFile(recpath.c_str(), "record", _NCT("rb"))))
		return init();
	size_t fsize = getFileSize(fp);
	if (fsize % sizeof(_records.front()) != 0)
	{
		PELOG_LOG((PLV_ERROR, "Invalid record size %zu\n", fsize));
		goto ERROR_CLEAR;
	}
	_records.resize(fsize / sizeof(_records.front()));
	if (_records.size() < 2)
	{
		fclose(fp);
		return init();
	}
	if (fread(_records.data(), sizeof(_records.front()), _records.size(), fp) != _records.size())
	{
		PELOG_LOG((PLV_ERROR, "Read record failed\n"));
		goto ERROR_CLEAR;
	}

	// load rname
	if (!(fp = OpenFile(recpath.c_str(), "rname", _NCT("rb"))))
		return init();
	fsize = getFileSize(fp);
	_rname.resize(fsize);
	if (fread(_rname.data(), 1, fsize, fp) != fsize)
	{
		PELOG_LOG((PLV_ERROR, "Read rname failed\n"));
		goto ERROR_CLEAR;
	}

	// load hist
	if (!(fp = OpenFile(recpath.c_str(), "hist", _NCT("rb"))))
		return init();
	fsize = getFileSize(fp);
	if (fsize % sizeof(_hists.front()) != 0)
	{
		PELOG_LOG((PLV_ERROR, "Invalid hist size %zu\n", fsize));
		goto ERROR_CLEAR;
	}
	_hists.resize(fsize / sizeof(_hists.front()));
	if (fread(_hists.data(), sizeof(_hists.front()), _hists.size(), fp) != _hists.size())
	{
		PELOG_LOG((PLV_ERROR, "Read hist failed\n"));
		goto ERROR_CLEAR;
	}

	// verify data
	{
		RootStat stat;
		if (!verifyrec(&stat))
		{
			PELOG_LOG((PLV_ERROR, "verify loaded failed\n"));
			goto ERROR_CLEAR;
		}
		PELOG_LOG((PLV_INFO, "Loaded file %u, dir %u, deleted (%u:%u), recycled %u\n",
			stat.nfile, stat.ndir, stat.ndelf, stat.ndeld, stat.nrecy));
	}

	// remote
	_remote = new RemoteDummy();

	return 0;

ERROR_CLEAR:
	if (fp)
		fclose(fp);
	_records.clear();
	_rname.clear();
	_hists.clear();
	return -1;
}

// create initial record settings
int Root::init()
{
	FILE *fp = NULL;
	_rname.resize(1);
	_rname[0] = 0;
	if ((fp = OpenFile(recpath.c_str(), "rname", _NCT("wb"))) == NULL)
		PELOG_ERROR_RETURN((PLV_ERROR, "Failed to open rname file to write\n"), -1);
	if (fwrite(_rname.data(), sizeof(_rname.front()), _rname.size(), fp) != _rname.size())
		PELOG_ERROR_RETURN((PLV_ERROR, "Write rname failed\n"), -1);
	fclose(fp);

	_hists.clear();
	if ((fp = OpenFile(recpath.c_str(), "hist", _NCT("wb"))) == NULL)
		PELOG_ERROR_RETURN((PLV_ERROR, "Failed to open hist file to write\n"), -1);
	fclose(fp);

	_records.clear();
	_records.resize(2);
	_records[1].isdir(true);
	if ((fp = OpenFile(recpath.c_str(), "record", _NCT("wb"))) == NULL)
		PELOG_ERROR_RETURN((PLV_ERROR, "Failed to open record file to write\n"), -1);
	fwrite(_records.data(), sizeof(_records.front()), _records.size(), fp);
	fclose(fp);

	return 0;
}

bool Root::verifyrec(Root::RootStat *stat) const
{
	RootStat tstat;
	if (!stat)
		stat = &tstat;
	memset(stat, 0, sizeof(stat));

	// recycle list
	uint32_t recyclelast = NULL;
	for (; _records[recyclelast].next(); recyclelast = _records[recyclelast].next())
		stat->nrecy++;
	// travel records
	std::stack<uint32_t> trace;
	uint32_t rid = 1;
	while (rid != 0 || trace.size() > 0)
	{
		if (rid == 0)
		{
			rid = _records[trace.top()].next();
			trace.pop();
			continue;
		}
		const RecordItem &r = _records[rid];
		if (rid == recyclelast)
			PELOG_ERROR_RETURN((PLV_ERROR, "recycle list corrupted\n"), false);
		if (r.name() >= _rname.size())
			PELOG_ERROR_RETURN((PLV_ERROR, "Invalid record name %u\n", rid), false);
		if (r.isdel())
			(r.isdir() ? stat->ndeld : stat->ndelf) ++;
		if (r.isdel() || !r.isdir())
		{
			for (uint32_t hid = r.hist(); hid; hid = _hists[hid].next())
				if (hid >= _hists.size())
					PELOG_ERROR_RETURN((PLV_ERROR, "Corrupted hist %u\n", rid), false);
		}
		if (!r.isdir())
		{
			if (!r.isdel())
				stat->nfile++;
			if (r.next() == 0)
				PELOG_ERROR_RETURN((PLV_ERROR, "Traceback missing %u\n", rid), false);
			rid = r.next();
			continue;
		}
		if (!r.isdel() && trace.size() > 0 && r.parent() != trace.top())
			PELOG_ERROR_RETURN((PLV_ERROR, "Invalid dir parent %u: \n", rid), false);
		if (!r.isdel() && r.next() == 0 && trace.size() > 1)
			PELOG_ERROR_RETURN((PLV_ERROR, "Traceback missing %u\n", rid), false);
		if (!r.isdel())
			stat->ndir++;
		if (r.sub())
		{
			trace.push(rid);
			rid = r.sub();
		}
		else
			rid = r.next();
	}
	return true;
}

bool Root::verifydir(uint32_t pid) const
{
	if (pid == 0 || !_records[pid].isdir())
		return false;
	enum { NONE, FILE, DIRDEL, DIR };
	int ltype = NONE;
	const char *lname = NULL;
	for (uint32_t rid = _records[pid].sub(); rid != 0; rid = _records[rid].next())
	{
		const RecordItem &ritem = _records[rid];
		int rtype = !ritem.isdir() ? FILE : (ritem.isdel() ? DIRDEL : DIR);
		const char *rname = getName(rid);
		if (rtype < ltype)	// type order
			return false;
		if (!*rname && (ritem.next() || !ritem.isdir() || ritem.isdel()))	// only loopback record has no name
			return false;
		if (rtype == DIR && ritem.parent() != pid)	// dir must have parent() set
			return false;
		if (rtype == ltype && *rname && pathCmpDp(lname, rname) >= 0)	// name must in ascending order
			return false;
		if (!ritem.next() && rtype != DIR)	// must has at least one dir item
			return false;
		ltype = rtype;
		lname = rname;
	}
	return true;
}

int Root::startRefresh()
{
	restate.clear();
	restate.resize(1);
	restate.back().rid = 1;
	redostate.clear();
	return 0;
}

// return: 0: finished, >0: one step, <0: error
int Root::refreshStep(int state, Action &action)
{
	action.type = Action::NONE;
	// check restate first
	if (restate.size() == 0)
		return 0;
	{
		if (restate[0].rid != 1)
		{
			restate.clear();
			PELOG_ERROR_RETURN((PLV_ERROR, "Invalid refresh root\n"), -1);
		}
		std::vector<const char *> pathparts = {root.c_str()};
		for (size_t i = 1; i < restate.size(); ++i)
		{
			RefreshIter &reiter = restate[i];
			RecordItem &rec = _records[reiter.rid];
			pathparts.push_back(rec.name(_rname));
			abuf<char> testpath;
			buildPath(pathparts.data(), pathparts.size(), testpath);
			if (rec.isdel() || !rec.isdir() || rec.parent() != restate[i - 1].rid ||
				reiter.stage != RefreshIter::INIT && pathCmpMt(reiter.path, testpath) != 0)
			{
				PELOG_LOG((PLV_ERROR, "Invalid refresh state %d (%s : %s). move back\n",
					(int)i, reiter.path.buf(), testpath.buf()));
				restate.resize(i + 1);
				reiter.stage = RefreshIter::REDOUPPER;
				break;
			}
		}
	}

	// run
	while (true)
	{
		RefreshIter &reiter = restate.back();
		RecordItem &rec = _records[reiter.rid];
		switch (reiter.stage)
		{
		case RefreshIter::INIT:
		{
			// store name
			reiter.name.scopyFrom(rec.name(_rname));
			// build path
			std::vector<const char *> pathparts;
			pathparts.push_back(root.c_str());
			for (size_t i = 1; i < restate.size(); ++i)
				pathparts.push_back(reiter.name);
			buildPath(pathparts.data(), pathparts.size(), reiter.path);
			// get dir contents
			if (ListDir(reiter.path, reiter.dirs, reiter.files) != 0)
			{
				// list dir failed. maybe it has just been deleted
				if (restate.size() <= 1)
					PELOG_LOG((PLV_ERROR, "Root dir listing failed: %s\n", root.c_str()));
				else
				{
					PELOG_LOG((PLV_WARNING, "Dir missing, REDO. %s\n", reiter.path.buf()));
					reiter.stage = RefreshIter::REDOUPPER;	// just rerun parent dir
				}
				break;
			}
			reiter.stage = RefreshIter::DELFILE;
			reiter.prog = 0;
			break;
		}

		case RefreshIter::DELFILE:
		{
			// look for next rec file
			if (reiter.prog == 0)
				reiter.prog = rec.sub();
			else	// already has prog, search for it under rec to ensure it is valid
			{
				uint32_t tgtid = rec.sub();
				for (; tgtid != 0 && !_records[tgtid].isdir(); tgtid = _records[tgtid].next())
					if (tgtid == reiter.prog)
						break;
				if (tgtid != reiter.prog)	// prog not found, skip to next stage
				{
					reiter.stage = RefreshIter::DELDIR;
					reiter.prog = 0;
					break;
				}
			}
			// for each rec file
			size_t fidx = 0;
			for (; reiter.prog != 0 && !_records[reiter.prog].isdir(); reiter.prog = _records[reiter.prog].next())
			{
				RecordItem &fitem = _records[reiter.prog];
				if (fitem.isdel())
					continue;
				while (fidx < reiter.files.size() && pathCmpMt(reiter.files[fidx].name, fitem.name(_rname)) < 0)
					++fidx;
				if (pathCmpMt(reiter.files[fidx].name, fitem.name(_rname)) != 0)
				{
					PELOG_LOG((PLV_DEBUG, "DEL file detected %s: %s\n", reiter.path.buf(), fitem.name(_rname)));
					reiter.prog = fitem.next();	// move forward before return, since prog is likely to be deleted
					AAssert(reiter.prog != 0);
					if (_records[reiter.prog].isdir())
					{
						reiter.stage = RefreshIter::DELDIR;
						reiter.prog = 0;
					}
					action.type = Action::DELFILE;
					buildPath(pathAbs2Rel(reiter.path.buf(), root.c_str()), fitem.name(_rname), action.name);
					return 1;
				}
			}
			// no more DELFILE if reach here, move on to next stage
			reiter.stage = RefreshIter::DELDIR;
			reiter.prog = 0;
			break;
		}

		case RefreshIter::DELDIR:
		{
			// look for next rec dir
			if (reiter.prog == 0)
			{
				for (reiter.prog = rec.sub();
					reiter.prog != 0 && (!_records[reiter.prog].isdir() || _records[reiter.prog].isdel());
					reiter.prog = _records[reiter.prog].next());
			}
			else	// already has prog, search for it under rec to ensure it is valid
			{
				uint32_t tgtid = rec.sub();
				for (; tgtid != 0; tgtid = _records[tgtid].next())
					if (tgtid == reiter.prog)
						break;
				reiter.prog = tgtid;
			}
			if (reiter.prog == 0 || !_records[reiter.prog].isdir() || _records[reiter.prog].isdel() ||
				!*_records[reiter.prog].name(_rname))	// no more to match, move on to next stage
			{
				reiter.stage = RefreshIter::FILE;
				reiter.prog = 0;
				break;
			}
			// for each rec dir
			size_t didx = 0;
			for (; reiter.prog != 0 && _records[reiter.prog].isdir() &&
				!_records[reiter.prog].isdel() && *_records[reiter.prog].name(_rname);
				reiter.prog = _records[reiter.prog].next())
			{
				RecordItem &ditem = _records[reiter.prog];
				while (didx < reiter.dirs.size() && pathCmpMt(reiter.dirs[didx].name, ditem.name(_rname)) < 0)
					++didx;
				if (pathCmpMt(reiter.dirs[didx].name, ditem.name(_rname)) != 0)
				{
					PELOG_LOG((PLV_DEBUG, "DEL dir detected %s: %s\n", reiter.path.buf(), ditem.name(_rname)));
					reiter.prog = ditem.next();	// move forward before return, since prog is likely to be deleted
					AAssert(reiter.prog == 0 || *_records[reiter.prog].name(_rname));
					if (reiter.prog == 0)	// no more rec dirs, move on
					{
						reiter.stage = RefreshIter::FILE;
						reiter.prog = 0;
					}
					action.type = Action::DELDIR;
					buildPath(pathAbs2Rel(reiter.path.buf(), root.c_str()), ditem.name(_rname), action.name);
					return 1;
				}
			}
			// no more DELFILE if reach here, move on to next stage
			reiter.stage = RefreshIter::FILE;
			reiter.prog = 0;
			break;
		}

		case RefreshIter::FILE:
		{
			uint32_t fid = rec.sub();
			if (fid != 0 && _records[fid].isdir())
				fid = 0;
			for (; reiter.prog < reiter.files.size(); ++reiter.prog)	// for each physical file
			{
				for (; fid != 0 && !_records[fid].isdir() &&
					pathCmpMt(_records[fid].name(_rname), reiter.files[reiter.prog].name) < 0;
					fid = _records[fid].next());
				if (fid != 0 && _records[fid].isdir())
					fid = 0;
				if (fid == 0 || _records[fid].isdel() ||
					pathCmpMt(_records[fid].name(_rname), reiter.files[reiter.prog].name) != 0)
				{
					PELOG_LOG((PLV_DEBUG, "ADD file detected %s: %s\n", reiter.path.buf(), reiter.files[reiter.prog].name));
					action.type = Action::ADDFILE;
					buildPath(pathAbs2Rel(reiter.path.buf(), root.c_str()), reiter.files[reiter.prog].name, action.name);
					reiter.prog++;	// move forward before return
					return 1;
				}
			}
			reiter.stage = RefreshIter::DIR;
			reiter.prog = 0;
			break;
		}

		case RefreshIter::DIR:
		{
			uint32_t did = rec.sub();
			while (did != 0 && (!_records[did].isdir() || _records[did].isdel()))	// look for the first record dir
				did = _records[did].next();
			for (; reiter.prog < reiter.dirs.size(); ++reiter.prog)	// for each physical dir
			{
				for (; did != 0 && _records[did].isdir() && getName(did) &&
					pathCmpMt(getName(did), reiter.dirs[reiter.prog].name) < 0;
					did = _records[did].next());	// look for a match
				if (did == 0 || !getName(did) || pathCmpMt(getName(did), reiter.dirs[reiter.prog].name) != 0)
				{
					PELOG_LOG((PLV_DEBUG, "ADD dir detected %s: %s\n", reiter.path.buf(), reiter.dirs[reiter.prog].name));
					action.type = Action::ADDDIR;
					buildPath(pathAbs2Rel(reiter.path.buf(), root.c_str()), reiter.dirs[reiter.prog].name, action.name);
					reiter.prog++;	// move forward before return
					return 1;
				}
			}
			reiter.stage = RefreshIter::RECUR;
			reiter.prog = 0;
			break;
		}

		case RefreshIter::RECUR:
			if (reiter.prog == 0)
				reiter.prog = rec.sub();
			while (reiter.prog != 0 && (!_records[reiter.prog].isdir() || _records[reiter.prog].isdel()))
				reiter.prog = _records[reiter.prog].next();
			if (reiter.prog != 0 && _records[reiter.prog].isdir() && !_records[reiter.prog].isdel())
			{
				uint32_t recurid = reiter.prog;
				reiter.prog = _records[reiter.prog].next();
				if (reiter.prog == 0)	// this is the last dir to recurse
					reiter.stage = RefreshIter::RETURN;
				restate.resize(restate.size() + 1);
				restate.back().rid = recurid;
				break;
			}
			reiter.stage = RefreshIter::RETURN;
			break;

		case RefreshIter::REDOUPPER:		// go back to parent dir and run again
			if (restate.size() <= 1)
				reiter.stage = RefreshIter::RETURN;
			else
			{
				restate.pop_back();
				restate.back().stage = RefreshIter::INIT;
				restate.back().files.clear();
				restate.back().dirs.clear();
				redostate[restate.back().rid]++;
				if (redostate[restate.back().rid] > 2)
				{
					PELOG_LOG((PLV_ERROR, "Too many redos. %s\n", restate.back().path.buf()));
					pelog_flush();
					abort();
				}
			}
			break;

		case RefreshIter::RETURN:	// finished current dir, go back to parent
			if (restate.size() <= 1)
			{
				restate.clear();
				return 0;
			}
			restate.pop_back();
			AAssert(restate.back().stage == RefreshIter::RECUR || restate.back().stage == RefreshIter::RETURN);
			break;
		default:
			AAssert(false);
			break;
		}
	}
	return 0;
}

// did: output the target directory id
int Root::addDir(const char *dir, size_t dlen, uint32_t &did)
{
	int res = OK;
	// process parents
	size_t baselen = splitPath(dir, dlen);
	uint32_t pid = 1;
	if (baselen > 0 && (res = addDir(dir, baselen, pid)) != OK)	// not top level dir, create parents
		return res;
	const char *name = baselen == 0 ? dir : dir + baselen + 1;
	size_t nlen = dlen - (name - dir);
	// check local
	FindResult dtype = FR_MATCH;
	did = findRecord(pid, name, nlen, FO_DIR, dtype);
	if (dtype == FR_MATCH && did != 0 && !_records[did].isdir())	// local is a file, error
		PELOG_ERROR_RETURN((PLV_ERROR, "Create dir failed. file exists. %.*s\n", dlen, dir), CONFLICT);
	if (dtype == FR_MATCH && did != 0 && _records[did].isdir())	// local already exists
		return OK;
	AAssert(dtype == FR_PRE && did != 0);
	uint32_t preid = did;
	// local not found. add remote first
	if ((res = _remote->addDir(dir, dlen)) != Remote::OK)
	{
		if (res != Remote::DISCONNECTED)
			PELOG_ERROR_RETURN((PLV_ERROR, "Create dir failed. remote error %d. %.*s\n", res, dlen, dir), REMOTEERR);
		else
			PELOG_ERROR_RETURN((PLV_TRACE, "Remote disconnected.\n"), DISCONNECTED);
	}
	// add local
	std::vector<uint32_t> cids;	// changed ids
	// look for a loopback record first
	RecPtr preptr;
	did = 0;
	for (preptr.set(pid, RPSUB); preptr(this) != 0; preptr.set(preptr(this), RPNEXT))
	{
		uint32_t lbid = preptr(this);
		if (_records[lbid].isdir() && !*getName(lbid))	// found loopback record
		{
			preptr(_records[lbid].next(), this);	// detach it
			did = lbid;	// use it as the new record
			cids.push_back(did);
			cids.push_back(preptr._id);
			break;
		}
	}
	if (did == 0)	// if no loopback record found, allocate a new record
		did = allocRec(cids);
	RecordItem &ditem = _records[did];
	ditem.name(allocRName(name, nlen));
	ditem.isdir(true);
	ditem.parent(pid);
	ditem.time(getDirTime(root.c_str(), dir, dlen));
	// insert the new record
	cids.push_back(preid);
	preptr.set(preid, preid == pid ? RPSUB : RPNEXT);
	ditem.next(preptr(this));
	preptr(did, this);
#ifdef _DEBUG
	AAssert(verifydir(pid));
#endif
	PELOG_LOG((PLV_INFO, "DIR ADDed %s : %.*s\n", root.c_str(), dlen, dir));
	return OK;
}

// look for the specific name under pid
// FO_FILE: !isdel file or dir, otherwise isdel file, otherwise pre
// FO_FILEDEL: isdel or !isdel file, otherwise 0
// FO_DIR: !isdel file or dir, otherwise pre
// FO_DIRDEL: isdel dir, otherwise 0
uint32_t Root::findRecord(uint32_t pid, const char *name, size_t namelen, FindOption option, FindResult &restype)
{
	AAssert(_records.size() >= 2 && namelen > 0 && _records[pid].isdir());
	uint32_t delid = 0, lid = pid, preid = pid;
	restype = FR_MATCH;
	for (uint32_t rid = _records[pid].sub(); rid != 0; lid = rid, rid = _records[rid].next())
	{
		int cmp = pathCmpDp(name, namelen, getName(rid));
		if (cmp == 0)
		{
			if (option == FO_FILE && !_records[rid].isdel() ||
				option == FO_FILEDEL && !_records[rid].isdir() ||
				option == FO_DIR && !_records[rid].isdel() ||
				option == FO_DIRDEL && _records[rid].isdir() && _records[rid].isdel())
				return rid;
			if (option == FO_FILE && !_records[rid].isdir())	// isdel() implied
				delid = rid;
		}
		if (option == FO_FILE && (rid == 0 || (!_records[rid].isdir() && cmp > 0)) ||
			option == FO_DIR && (!_records[rid].isdir() || _records[rid].isdel() || (*getName(rid) && cmp > 0)))
			preid = rid;
	}
	if (delid != 0)
		return delid;
	if (option == FO_FILE || option == FO_DIR)
	{
		restype = FR_PRE;
		return preid;
	}
	return 0;
}

// alloc string in _rname, and write to disk
uint32_t Root::allocRName(const char *name, uint32_t len)
{
	// alloc in memory
	uint32_t base = _rname.size();
	AAssert(base + len + 1 > base);
	_rname.resize(base + len + 1);
	memcpy(&_rname[base], name, len);
	_rname[base + len] = 0;
	// write back
	FILE *fp = OpenFile(recpath.c_str(), "rname", _NCT("rb+"));
	AAssert(fp);
	fseek(fp, base, SEEK_SET);
	AAssert(ftell(fp) == base);
	size_t res = fwrite(&_rname[base], 1, len + 1, fp);
	AAssert(res == len + 1);
	fclose(fp);
	return base;
}

// alloc a new item in _record. change in memory only, use writeRec() to write to disk
uint32_t Root::allocRec(std::vector<uint32_t> &cids)
{
	// TODO: look in recycled
	uint32_t nid = _records.size();
	_records.resize(nid + 1);
	_records[nid].clear();
	cids.push_back(nid);
	return nid;
}

// write back records to file
int Root::writeRec(std::vector<uint32_t> &cids)
{
	std::sort(cids.begin(), cids.end());
	FILE *fp = OpenFile(recpath.c_str(), "record", _NCT("rb+"));
	AAssert(fp);
	uint32_t lid = -1;
	for (uint32_t cid : cids)
	{
		if (cid == lid)
			continue;
		lid = cid;
		fseek(fp, sizeof(_records[0]) * cid, SEEK_SET);
		AAssert(ftell(fp) == sizeof(_records[0]) * cid);
		size_t res = fwrite(&_records[cid], sizeof(_records[cid]), 1, fp);
		AAssert(res == 1);
	}
	return 0;
}

int Root::perform(Action &action)
{
	switch (action.type)
	{
	case Action::ADDDIR:
		return addDir(action.name);
	}
	PELOG_ERROR_RETURN((PLV_WARNING, "Unsupported action %d\n", action.type), 0);
}