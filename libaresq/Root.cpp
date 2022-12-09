#include "stdafx.h"
#include "Root.h"
#include <stack>
#include <algorithm>
#include <map>
#include "fsadapter.h"
#include "utfconv.h"
#include "pe_log.h"

//#define DRY_RUN	// DO NOT write back changes
//#define FRESH_DEBUG	// DO NOT load previously saved data

Root::Root(const char *name, const char *root, const char *recpath) :
	_name(name), _localroot(root), recpath(recpath)
{

}

static inline size_t getFileSize(FILE *fp)
{
	size_t pos = ftell(fp);
	fseek(fp, 0, SEEK_END);
	size_t size = ftell(fp);
	fseek(fp, pos, SEEK_SET);
	return size;
}

Root::~Root()
{
#if defined(_DEBUG) && !defined(DRY_RUN)
	// verify saved data on exit
	abuf<char> buf;
	FILE *fp = NULL;
	// records
	AuVerify(fp = OpenFile(recpath.c_str(), "record", _NCT("rb")));
	size_t fsize = getFileSize(fp);
	AuVerify(fsize == _records.size() * sizeof(_records[0]));
	buf.resize(fsize > 0 ? fsize : 1);
	AuVerify(fsize == fread(buf, 1, fsize, fp));
	fclose(fp);
	AuVerify(memcmp(buf, _records.data(), fsize) == 0);
	// rname
	AuVerify(fp = OpenFile(recpath.c_str(), "rname", _NCT("rb")));
	fsize = getFileSize(fp);
	AuVerify(fsize == _rname.size() * sizeof(_rname[0]));
	buf.resize(fsize > 0 ? fsize : 1);
	AuVerify(fsize == fread(buf, 1, fsize, fp));
	fclose(fp);
	AuVerify(memcmp(buf, _rname.data(), fsize) == 0);
	// hists
	AuVerify(fp = OpenFile(recpath.c_str(), "hist", _NCT("rb")));
	fsize = getFileSize(fp);
	AuVerify(fsize == _hists.size() * sizeof(_hists[0]));
	buf.resize(fsize > 0 ? fsize : 1);
	AuVerify(fsize == fread(buf, 1, fsize, fp));
	fclose(fp);
	AuVerify(memcmp(buf, _hists.data(), fsize) == 0);
#endif
}

int Root::load()
{
	if (CreateDir(recpath.c_str()) != 0)
		PELOG_ERROR_RETURN((PLV_ERROR, "Create RECPATH failed\n"), -1);

	FILE *fp = NULL;

#ifndef FRESH_DEBUG
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
#else
	_records.clear();
	_records.resize(2);
	_records[1].isdir(true);
	_rname.resize(1);
	_hists.clear();
#endif

	// verify data
	{
		RootStat stat;
		if (!verifyrec(&stat))
		{
			PELOG_LOG((PLV_ERROR, "verify loaded failed\n"));
			AuAssert(false);
			goto ERROR_CLEAR;
		}
		PELOG_LOG((PLV_INFO, "Loaded file %u, dir %u, deleted (%u:%u), recycled %u\n",
			stat.nfile, stat.ndir, stat.ndelf, stat.ndeld, stat.nrecy));
	}

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
	uint32_t recyclelast = 0;
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
				PELOG_ERROR_RETURN((PLV_ERROR, "Loopback missing %u\n", rid), false);
			rid = r.next();
			continue;
		}
		if (!r.isdel() && trace.size() > 0 && r.parent() != trace.top())
			PELOG_ERROR_RETURN((PLV_ERROR, "Invalid dir parent %u: \n", rid), false);
		if (r.isdel() && r.next() == 0 && trace.size() > 1)
			PELOG_ERROR_RETURN((PLV_ERROR, "Loopback missing %u\n", rid), false);
		if (!r.isdel() && *r.name(_rname))
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
	// TODO: do some cleanup if state is not OK
	if (state != OK)
		return state;

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
		std::vector<const char *> pathparts = {_localroot.c_str()};
		for (size_t i = 1; i < restate.size(); ++i)
		{
			RefreshIter &reiter = restate[i];
			RecordItem &rec = _records[reiter.rid];
			AuVerify(*rec.name(_rname));
			//pathparts.push_back(rec.name(_rname));
			//abuf<char> testpath;
			//buildPath(pathparts.data(), pathparts.size(), testpath);
			if (rec.isdel() || !rec.isdir() || rec.parent() != restate[i - 1].rid ||
				reiter.stage != RefreshIter::INIT && pathCmpMt(reiter.name, getName(reiter.rid)) != 0)
			{
				PELOG_LOG((PLV_ERROR, "Invalid refresh state %d (%s : %s : %s). move back\n",
					(int)i, reiter.path.buf(), reiter.name.buf(), getName(reiter.rid)));
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
			AuVerify(rec.isdir() && !rec.isdel() && (reiter.rid == 1 || *rec.name(_rname)));
			// build path
			std::vector<const char *> pathparts;
			pathparts.push_back(_localroot.c_str());
			for (size_t i = 1; i < restate.size(); ++i)
				pathparts.push_back(restate[i].name);
			buildPath(pathparts.data(), pathparts.size(), reiter.path);
			// get dir contents
			if (ListDir(reiter.path, reiter.dirs, reiter.files) != 0)
			{
				// list dir failed. maybe it has just been deleted
				if (restate.size() <= 1)
					PELOG_LOG((PLV_ERROR, "Root dir listing failed: %s\n", _localroot.c_str()));
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
			size_t fidx = 0;	// current idx in physical files
			// for each rec file
			for (; reiter.prog != 0 && !_records[reiter.prog].isdir(); reiter.prog = _records[reiter.prog].next())
			{
				RecordItem &fitem = _records[reiter.prog];
				if (fitem.isdel())
					continue;
				// look for the matched item in physical files for the rec file
				while (fidx < reiter.files.size() && pathCmpMt(reiter.files[fidx].name, fitem.name(_rname)) < 0)
					++fidx;
				// if not found
				if (fidx >= reiter.files.size() || pathCmpMt(reiter.files[fidx].name, fitem.name(_rname)) != 0)
				{
					PELOG_LOG((PLV_DEBUG, "DEL file detected %s: %s\n", reiter.path.buf(), fitem.name(_rname)));
					reiter.prog = fitem.next();	// move forward before return, since prog is likely to be deleted
					AuVerify(reiter.prog != 0);
					if (_records[reiter.prog].isdir())
					{
						reiter.stage = RefreshIter::DELDIR;
						reiter.prog = 0;
					}
					action.type = Action::DELFILE;
					buildPath(pathAbs2Rel(reiter.path.buf(), _localroot.c_str()), fitem.name(_rname), action.name);
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
			size_t didx = 0;	// current idx in physical files
			// for each rec dir
			for (; reiter.prog != 0 && _records[reiter.prog].isdir() &&
				!_records[reiter.prog].isdel() && *_records[reiter.prog].name(_rname);
				reiter.prog = _records[reiter.prog].next())
			{
				RecordItem &ditem = _records[reiter.prog];
				// look for the matched item in physical dirs for the rec dir
				while (didx < reiter.dirs.size() && pathCmpMt(reiter.dirs[didx].name, ditem.name(_rname)) < 0)
					++didx;
				// if not found
				if (didx >= reiter.dirs.size() || pathCmpMt(reiter.dirs[didx].name, ditem.name(_rname)) != 0)
				{
					PELOG_LOG((PLV_DEBUG, "DEL dir detected %s: %s\n", reiter.path.buf(), ditem.name(_rname)));
					reiter.prog = ditem.next();	// move forward before return, since prog is likely to be deleted
					AuVerify(reiter.prog == 0 || *_records[reiter.prog].name(_rname));
					if (reiter.prog == 0)	// no more rec dirs, move on
					{
						reiter.stage = RefreshIter::FILE;
						reiter.prog = 0;
					}
					action.type = Action::DELDIR;
					buildPath(pathAbs2Rel(reiter.path.buf(), _localroot.c_str()), ditem.name(_rname), action.name);
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
					buildPath(pathAbs2Rel(reiter.path.buf(), _localroot.c_str()), reiter.files[reiter.prog].name, action.name);
					reiter.prog++;	// move forward before return
					return 1;
				}
				else if (_records[fid].sizeChanged(reiter.files[reiter.prog].size) ||
					abs((int64_t)_records[fid].time() - (int64_t)reiter.files[reiter.prog].time) > 10)
				{
					PELOG_LOG((PLV_DEBUG, "MOD file detected %s: %s\n", reiter.path.buf(), reiter.files[reiter.prog].name));
					action.type = Action::ADDFILE;
					buildPath(pathAbs2Rel(reiter.path.buf(), _localroot.c_str()), reiter.files[reiter.prog].name, action.name);
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
					buildPath(pathAbs2Rel(reiter.path.buf(), _localroot.c_str()), reiter.dirs[reiter.prog].name, action.name);
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
			if (reiter.prog != 0 && _records[reiter.prog].isdir() && !_records[reiter.prog].isdel() && *getName(reiter.prog))
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
					AuVerify(false);
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
			AuVerify(restate.back().stage == RefreshIter::RECUR || restate.back().stage == RefreshIter::RETURN);
			break;

		default:
			AuVerify(false);
			break;
		}
	}
	return 0;
}

// did: output the target directory record id
int Root::addDir(const char *dir, size_t dlen, uint32_t &did, Remote *remote)
{
	int res = OK;
	// process parents
	size_t baselen = splitPath(dir, dlen);
	uint32_t pid = 1;
	if (baselen > 0 && (res = addDir(dir, baselen, pid, remote)) != OK)	// not top level dir, create parents
		return res;
	const char *dirname = baselen == 0 ? dir : dir + baselen + 1;
	size_t nlen = dlen - (dirname - dir);
	// check local
	FindResult dtype = FR_MATCH;
	did = findRecord(pid, dirname, nlen, FO_DIR, dtype);
	if (dtype == FR_MATCH && did != 0 && !_records[did].isdir())	// local is a file, error
		PELOG_ERROR_RETURN((PLV_ERROR, "Create dir failed. file exists. %.*s\n", dlen, dir), CONFLICT);
	if (dtype == FR_MATCH && did != 0 && _records[did].isdir())	// local already exists
		return OK;
	AuVerify(dtype == FR_PRE && did != 0);
	uint32_t preid = did;
	// local not found. add remote first
	if ((res = remote->addDir(_name.c_str(), std::string(dir, dlen).c_str())) != Remote::OK)
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
			PELOG_LOG((PLV_DEBUG, "Reusing a loopback record %u\n", lbid));
			AuVerify(_records[lbid].next() == 0 && _records[lbid].parent() == pid && preptr._id != pid &&
				(!_records[preptr._id].isdir() || _records[preptr._id].isdel()));
			preptr(_records[lbid].next(), this);	// detach it, and atatch back later, for simplicity
			did = lbid;	// use it as the new record
			cids.push_back(did);
			cids.push_back(preptr._id);
			break;
		}
	}
	if (did == 0)	// if no loopback record found, allocate a new record
		did = allocRec(cids);
	RecordItem &ditem = _records[did];
	ditem.name(allocRName(dirname, nlen));
	ditem.isdir(true);
	ditem.parent(pid);
	ditem.time(getDirTime(_localroot.c_str(), dir, dlen));
	// insert the new record
	cids.push_back(preid);
	preptr.set(preid, preid == pid ? RPSUB : RPNEXT);
	ditem.next(preptr(this));
	preptr(did, this);
	AuAssert(verifydir(pid));
	writeRec(cids);
	PELOG_LOG((PLV_INFO, "DIR ADDed(%u) %s : %.*s\n", did, _localroot.c_str(), dlen, dir));
	return OK;
}

// fid: output the target file record id
int Root::addFile(const char *file, size_t flen, uint32_t &fid, Remote *remote)
{
	// TODO: manage history versions
	int res = OK;
	// process parents
	size_t baselen = splitPath(file, flen);
	uint32_t pid = 1;
	if (baselen > 0 && (res = addDir(file, baselen, pid, remote)) != OK)	// not in top level dir, create parents
		return res;
	const char *filename = baselen == 0 ? file : file + baselen + 1;
	size_t nlen = flen - (filename - file);
	// get attr
	uint32_t ftime = 0;
	uint64_t fsize = 0;
	if (getFileAttr(_localroot.c_str(), file, flen, ftime, fsize) != 0)
		PELOG_ERROR_RETURN((PLV_ERROR, "Get file attr failed. %s : %.*s\n", _localroot.c_str(), flen, file), NOTFOUND);
	PELOG_LOG((PLV_TRACE, "FILE size %llu time %u. %s : %.*s\n", fsize, ftime, _localroot.c_str(), flen, file));
	// check local
	FindResult dtype = FR_MATCH;
	fid = findRecord(pid, filename, nlen, FO_FILE, dtype);
	if (dtype == FR_MATCH && fid != 0 && _records[fid].isdir())	// local is a dir, error
		PELOG_ERROR_RETURN((PLV_ERROR, "addFile failed. dir exists. %.*s\n", flen, file), CONFLICT);
	if (dtype == FR_MATCH && fid != 0 && !_records[fid].isdir() &&
			_records[fid].time() == ftime && !_records[fid].sizeChanged(fsize))	// local already exists & no change
		return OK;
	bool isnew = !(dtype == FR_MATCH && fid != 0 && !_records[fid].isdir());
	AuVerify(fid != 0);
	uint32_t preid = dtype == FR_PRE ? fid : 0;
	fid = dtype == FR_MATCH ? fid : 0;
	// add remote first
	if ((res = remote->addFile(_localroot.c_str(), _name.c_str(), std::string(file, flen).c_str())) != Remote::OK)
	{
		if (res != Remote::DISCONNECTED)
			PELOG_ERROR_RETURN((PLV_ERROR, "addFile failed. remote error %d. %.*s\n", res, flen, file), REMOTEERR);
		else
			PELOG_ERROR_RETURN((PLV_TRACE, "Remote disconnected.\n"), DISCONNECTED);
	}
	// add local
	std::vector<uint32_t> cids;	// changed ids
	RecPtr preptr;
	if (fid != 0)	// already exists, reuse it and look for pre
	{
		for (preptr.set(pid, RPSUB); preptr(this) != 0 && preptr(this) != fid; preptr.set(preptr(this), RPNEXT))
		{
			if (preptr(this) == fid)
				break;
		}
		AuVerify(preptr(this) == fid);
	}
	else
	{
		AuVerify(preid != 0);
		preptr.set(preid, preid == pid ? RPSUB : RPNEXT);
		fid = allocRec(cids);
		_records[fid].name(allocRName(filename, nlen));
		_records[fid].next(preptr(this));
		preptr(fid, this);
		cids.push_back(preid);
	}
	cids.push_back(fid);
	RecordItem &fitem = _records[fid];
	fitem.isdir(false);
	fitem.time(ftime);
	fitem.size24(fsize);
	fitem.isdel(false);
	// add a loopback record if there isn't one
	for (preptr.set(fid, RPNEXT); preptr(this) != 0; preptr.set(preptr(this), RPNEXT))
		;	// look for the last record in this dir
	if (!_records[preptr._id].isdir() || _records[preptr._id].isdel() || *getName(preptr._id))	// no loopback
	{
		uint32_t lbid = allocRec(cids);
		_records[lbid].isdir(true);
		_records[lbid].parent(pid);
		cids.push_back(preptr._id);
		preptr(lbid, this);
		PELOG_LOG((PLV_DEBUG, "Loopback record ADDed(%u)\n", lbid, _localroot.c_str()));
	}
	AuAssert(verifydir(pid));
	writeRec(cids);
	PELOG_LOG((PLV_INFO, "FILE %s(%u) %s : %.*s\n", isnew ? "ADDed" : "MODed", fid, _localroot.c_str(), flen, file));
	return OK;
}

int Root::delDir(const char *dir, size_t dlen, Remote *remote)
{
	// find parent id
	FindResult foundtype = FR_MATCH;
	uint32_t pid = 0;
	uint32_t did = findRecord(dir, dlen, FO_DIR, foundtype, pid);
	if (did == 0 || pid == 0 || foundtype != FR_MATCH || !_records[did].isdir() || _records[did].isdel())
		PELOG_ERROR_RETURN((PLV_ERROR, "recdir not found %s : %.*s\n", _localroot.c_str(), dlen, dir), NOTFOUND);
	return delDir(did, pid, dir, dlen, remote);
}

int Root::delDir(uint32_t rid, uint32_t pid, const char *dir, size_t dlen, Remote *remote)
{
	// TODO: hist
	// TODO: verify physical dir existance
	AuVerify(!_records[rid].isdel() && _records[rid].parent() == pid);	// not implemented yet
	AuVerify(_records[rid].isdir() && *dir && dlen > 0 && *getName(rid));
	std::vector<uint32_t> cids;	// changed ids
	int res = OK;
	if (_histnum == 0)
	{
		// delete sub records
		while (_records[rid].sub())
		{
			uint32_t sid = _records[rid].sub();
			RecordItem &sub = _records[sid];
			abuf<char> subname;
			if (*getName(sid))
				buildPath(dir, dlen, getName(sid), strlen(getName(sid)), subname);
			if (!sub.isdir() && !sub.isdel())	// a regular file
			{
				if ((res = delFile(sid, rid, subname, strlen(subname), remote)) != OK)
					return res;
			}
			else if (sub.isdir() && !sub.isdel() && *getName(sid))	// a regular dir
			{
				if ((res = delDir(sid, rid, subname, strlen(subname), remote)) != OK)
					return res;
			}
			else if (sub.isdir() && !sub.isdel())	// a loop back record. this should no happen
				AuVerify(false);
			else
			{
				AuVerify(sub.isdel());
				AuVerify(false);		// not supported yet
			}
		}	// while (_records[rid].sub())
		// must be an empty dir if reaches here
		// delete remote
		if ((res = remote->delDir(_name.c_str(), std::string(dir, dlen).c_str())) != Remote::OK)
		{
			if (res != Remote::DISCONNECTED)
				PELOG_ERROR_RETURN((PLV_ERROR, "Del file failed. remote error %d. %.*s\n", res, dlen, dir), REMOTEERR);
			else
				PELOG_ERROR_RETURN((PLV_TRACE, "Remote disconnected.\n"), DISCONNECTED);
		}
		// delete record
		RecPtr preptr;
		for (preptr.set(pid, RPSUB); preptr(this) != rid; preptr.set(preptr(this), RPNEXT))
			;	// look for pre
		AuVerify(preptr(this) == rid);
		cids.push_back(rid);
		if (!_records[rid].next() && preptr._type != RPSUB && (!_records[preptr._id].isdir() || _records[preptr._id].isdel()))
		{
			// parent has more than one children and only one subdir. convert dir into a loopback record
			AuVerify(eraseName(rid) == 0);
			PELOG_LOG((PLV_INFO, "DIR trans to loopback(%u)\n", rid));
		}
		else
		{
			// detach
			cids.push_back(preptr._id);
			preptr(_records[rid].next(), this);
			// recycle
			recycleRec(rid, cids);
		}
	}
	else
	{
		AuVerify(false);		// not implemented
	}
	AuAssert(verifydir(pid));
	writeRec(cids);
	PELOG_LOG((PLV_INFO, "DIR DELed(%u) %s : %.*s\n", rid, _localroot.c_str(), dlen, dir));
	return OK;
}

int Root::delFile(const char *filename, size_t flen, Remote *remote)
{
	// TODO: hist
	// TODO: verify physical file existance
	// find parent id
	FindResult foundtype = FR_MATCH;
	uint32_t pid = 0;
	uint32_t fid = findRecord(filename, flen, FO_FILE, foundtype, pid);
	if (fid == 0 || pid == 0 || foundtype != FR_MATCH || _records[fid].isdir() || _records[fid].isdel())
		PELOG_ERROR_RETURN((PLV_ERROR, "recfile not found %s : %.*s\n", _localroot.c_str(), flen, filename), NOTFOUND);
	return delFile(fid, pid, filename, flen, remote);
}

int Root::delFile(uint32_t rid, uint32_t pid, const char *filename, size_t flen, Remote *remote)
{
	AuVerify(!_records[rid].isdel());	// not implemented yet
	AuVerify(!_records[rid].isdir() && *filename && *getName(rid));
	std::vector<uint32_t> cids;	// changed ids
	int res = 0;
	if (_histnum == 0)
	{
		// del remote
		if ((res = remote->delFile(_name.c_str(), std::string(filename, flen).c_str())) != Remote::OK)
		{
			if (res != Remote::DISCONNECTED)
				PELOG_ERROR_RETURN((PLV_ERROR, "Del file failed. remote error %d. %.*s\n", res, flen, filename), REMOTEERR);
			else
				PELOG_ERROR_RETURN((PLV_TRACE, "Remote disconnected.\n"), DISCONNECTED);
		}
		// del local
		RecPtr preptr;
		for (preptr.set(pid, RPSUB); preptr(this) != rid; preptr.set(preptr(this), RPNEXT))
			;	// look for pre
		AuVerify(preptr(this) == rid);
		// detach
		cids.push_back(rid);
		cids.push_back(preptr._id);
		preptr(_records[rid].next(), this);
		// recycle
		recycleRec(rid, cids);
		// if there is no more file/dir under parent, also delete the loopback record
		if (preptr._id == pid && _records[pid].sub() != 0 && _records[_records[pid].sub()].isdir() &&
			!*getName(_records[pid].sub()))
		{
			AuVerify(!_records[_records[pid].sub()].isdel() && _records[_records[pid].sub()].next() == 0);
			cids.push_back(pid);
			recycleRec(_records[pid].sub(), cids);
			PELOG_LOG((PLV_DEBUG, "Loopback record DELed(%u)\n", _records[pid].sub()));
			_records[pid].sub(0);
		}
	}
	else
		AuVerify(false);		// not implemented
	AuAssert(verifydir(pid));
	writeRec(cids);
	PELOG_LOG((PLV_INFO, "FILE DELed(%u) %s : %.*s\n", rid, _localroot.c_str(), flen, filename));
	return OK;
}

// look for the specific name under pid
// FO_FILE: !isdel file or dir, otherwise isdel file, otherwise pre
// FO_FILEDEL: isdel or !isdel file, otherwise 0
// FO_DIR: !isdel file or dir, otherwise pre
// FO_DIRDEL: isdel dir, otherwise 0
uint32_t Root::findRecord(uint32_t pid, const char *name, size_t namelen, FindOption option, FindResult &restype)
{
	AuVerify(_records.size() >= 2 && namelen > 0 && _records[pid].isdir());
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

uint32_t Root::findRecord(const char *name, size_t namelen, FindOption option, FindResult &restype, uint32_t &pid)
{
	pid = 1;
	restype = FR_MATCH;
	size_t baselen = splitPath(name, namelen);
	if (baselen == 0)	// in root
		return findRecord(pid, name, namelen, option, restype);
	// look for parent id
	{
		uint32_t tmpid = 0;
		FindResult tmptype = FR_MATCH;
		pid = findRecord(name, baselen, FO_DIR, tmptype, tmpid);
		if (pid == 0 || tmptype != FR_MATCH || !_records[pid].isdir() || _records[pid].isdel())	// parent not found
		{
			pid = 0;
			return 0;
		}
	}
	return findRecord(pid, name + baselen + 1, namelen - baselen - 1, option, restype);
}

// alloc string in _rname, and write to disk
uint32_t Root::allocRName(const char *name, uint32_t len)
{
	// alloc in memory
	uint32_t base = _rname.size();
	AuVerify(base + len + 1 > base);
	_rname.resize(base + len + 1);
	memcpy(&_rname[base], name, len);
	_rname[base + len] = 0;
	// write back
#ifndef DRY_RUN
	FILE *fp = OpenFile(recpath.c_str(), "rname", _NCT("rb+"));
	AuVerify(fp);
	fseek(fp, base, SEEK_SET);
	AuVerify(ftell(fp) == base);
	size_t res = fwrite(&_rname[base], 1, len + 1, fp);
	AuVerify(res == len + 1);
	fclose(fp);
#endif
	return base;
}

// alloc a new item in _record. change in memory only, use writeRec() to write to disk
uint32_t Root::allocRec(std::vector<uint32_t> &cids)
{
	uint32_t nid = 0;
	if (_records[0].next())	// found in recycled
	{
		nid = _records[0].next();
		cids.push_back(0);
		_records[0].next(_records[nid].next());
		PELOG_LOG((PLV_DEBUG, "Reusing recycled record %u\n", nid));
	}
	else	// create new
	{
		nid = _records.size();
		_records.resize(nid + 1);
	}
	_records[nid].clear();
	cids.push_back(nid);
	return nid;
}

int Root::eraseName(uint32_t rid)
{
	if (!*getName(rid))
	{
		AuVerify(_records[rid].next() == 0);		// only loopback records are noname
		return 0;
	}
	size_t nlen = strlen(getName(rid));
	memset(&_rname[_records[rid].name()], 0, nlen);
#ifndef DRY_RUN
	FILE *fp = OpenFile(recpath.c_str(), "rname", _NCT("rb+"));
	AuVerify(fp);
	fseek(fp, _records[rid].name(), SEEK_SET);
	AuVerify(ftell(fp) == _records[rid].name());
	size_t res = fwrite(getName(rid), 1, nlen, fp);
	AuVerify(res == nlen);
	fclose(fp);
#endif
	_records[rid].name(0u);
	return OK;
}

int Root::recycleRec(uint32_t rid, std::vector<uint32_t> &cids)
{
	cids.push_back(rid);
	// clear name
	AuVerify(eraseName(rid) == 0);
	// look for pos in recycle list
	uint32_t preid = 0;
	for (preid = 0; _records[preid].next() != 0 && _records[preid].next() < rid; preid = _records[preid].next())
		;
	// insert
	_records[rid].name((uint32_t)0);
	_records[rid].isdir(true);
	_records[rid].isdel(false);
	_records[rid].parent(0);
	_records[rid].next(_records[preid].next());
	_records[preid].next(rid);
	cids.push_back(preid);
	return 0;
}

// write back records to file
int Root::writeRec(std::vector<uint32_t> &cids)
{
#ifndef DRY_RUN
	std::sort(cids.begin(), cids.end());
	FILE *fp = OpenFile(recpath.c_str(), "record", _NCT("rb+"));
	AuVerify(fp);
	uint32_t lid = -1;
	for (uint32_t cid : cids)
	{
		if (cid == lid)
			continue;
		lid = cid;
		fseek(fp, sizeof(_records[0]) * cid, SEEK_SET);
		AuVerify(ftell(fp) == sizeof(_records[0]) * cid);
		size_t res = fwrite(&_records[cid], sizeof(_records[cid]), 1, fp);
		AuVerify(res == 1);
	}
	fclose(fp);
#endif
	return 0;
}

int Root::perform(Action &action, Remote *remote)
{
	uint32_t rid = 0;
	switch (action.type)
	{
	case Action::ADDDIR:
		return addDir(action.name, strlen(action.name), rid, remote);
	case Action::ADDFILE:
	case Action::MODFILE:
		return addFile(action.name, strlen(action.name), rid, remote);
	case Action::DELDIR:
		return delDir(action.name, strlen(action.name), remote);
	case Action::DELFILE:
		return delFile(action.name, strlen(action.name), remote);
	}
	PELOG_ERROR_RETURN((PLV_WARNING, "Unsupported action %d\n", action.type), NOTIMPLEMENTED);
}