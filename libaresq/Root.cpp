#include "stdafx.h"
#include "Root.h"
#include <stack>
#include <algorithm>
#include <map>
#include "Aresq.h"
#include "fsadapter.h"
#include "utfconv.h"
#include "resguard.h"
#include "pe_log.h"

//#define DRY_RUN	// DO NOT write back changes
//#define FRESH_DEBUG	// DO NOT load previously saved data

Root::Root()
{
}

static inline uint64_t getFileSize(FILE *fp)
{
	uint64_t pos = ftell(fp);
	fseek(fp, 0, SEEK_END);
	uint64_t size = ftell(fp);
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
	size_t fsize = (size_t)getFileSize(fp);
	AuVerify(fsize == _records.size() * sizeof(_records[0]));
	buf.resize(fsize > 0 ? fsize : 1);
	AuVerify(fsize == fread(buf, 1, fsize, fp));
	fclose(fp);
	AuVerify(memcmp(buf, _records.data(), fsize) == 0);
	// rname
	AuVerify(fp = OpenFile(recpath.c_str(), "rname", _NCT("rb")));
	fsize = (size_t)getFileSize(fp);
	AuVerify(fsize == _rname.size() * sizeof(_rname[0]));
	buf.resize(fsize > 0 ? fsize : 1);
	AuVerify(fsize == fread(buf, 1, fsize, fp));
	fclose(fp);
	AuVerify(memcmp(buf, _rname.data(), fsize) == 0);
#endif
}

int Root::load(int id, const char *name, const char *root, const char *rec_path, AresqIgnore *aresqignore)
{
	rootid = id;
	_name = name;
	_localroot = root;
	recpath = rec_path;
	ignore = aresqignore;

	if (CreateDir(recpath.c_str()) != 0)
		PELOG_ERROR_RETURN((PLV_ERROR, "Create RECPATH failed\n"), -1);

	FILE *fp = NULL;

#ifndef FRESH_DEBUG
	// load record
	if (!(fp = OpenFile(recpath.c_str(), "record", _NCT("rb"))))
		return init();
	size_t fsize = (size_t)getFileSize(fp);
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
	fsize = (size_t)getFileSize(fp);
	_rname.resize(fsize);
	if (fread(_rname.data(), 1, fsize, fp) != fsize)
	{
		PELOG_LOG((PLV_ERROR, "Read rname failed\n"));
		goto ERROR_CLEAR;
	}

#else
	init();
	//_records.clear();
	//_records.resize(2);
	//_records[1].isdir(true);
	//_rname.resize(1);
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
		PELOG_LOG((PLV_INFO, "Loaded file %u, dir %u, recycled %u\n",
			stat.nfile, stat.ndir, stat.nrecy));
	}

	return 0;

ERROR_CLEAR:
	if (fp)
		fclose(fp);
	_records.clear();
	_rname.clear();
	return -1;
}

// create initial record settings
int Root::init()
{
	_rname.resize(1);
	_rname[0] = 0;

	_records.clear();
	_records.resize(2);
	_records[1].isdir(true);
	_records[1].isactive(true);

#ifndef DRY_RUN
	FILEGuard fp = NULL;
	if (!(fp = OpenFile(recpath.c_str(), "rname", _NCT("wb"))))
		PELOG_ERROR_RETURN((PLV_ERROR, "Failed to open rname file to write\n"), -1);
	if (fwrite(_rname.data(), sizeof(_rname.front()), _rname.size(), fp) != _rname.size())
		PELOG_ERROR_RETURN((PLV_ERROR, "Write rname failed\n"), -1);
	fp.release();
	if (!(fp = OpenFile(recpath.c_str(), "record", _NCT("wb"))))
		PELOG_ERROR_RETURN((PLV_ERROR, "Failed to open record file to write\n"), -1);
	if (fwrite(_records.data(), sizeof(_records.front()), _records.size(), fp) != _records.size())
		PELOG_ERROR_RETURN((PLV_ERROR, "Write records failed\n"), -1);
	fp.release();
#endif

	return 0;
}

bool Root::verifyrec(Root::RootStat *stat) const
{
	RootStat tstat;
	if (!stat)
		stat = &tstat;
	memset(stat, 0, sizeof(stat));

	// recycle list
	stat->nrecy = 1;
	for (uint32_t recid = _records[0].next(); recid; recid = _records[recid].next())
	{
		if (_records[recid].isactive())
			PELOG_ERROR_RETURN((PLV_ERROR, "recycle list corrupted\n"), false);
		stat->nrecy++;
	}
	// travel records
	std::stack<uint32_t> trace;
	uint32_t rid = 1;
	while (rid != 0 || trace.size() > 0)
	{
		if (rid == 0)
		{
			rid = trace.top();
			trace.pop();
			rid = _records[rid].islast() ? 0 : _records[rid].next();
			continue;
		}
		const RecordItem &r = _records[rid];
		if (!r.isactive())
			PELOG_ERROR_RETURN((PLV_ERROR, "Invalid record state %u\n", rid), false);
		if (r.name() >= _rname.size())
			PELOG_ERROR_RETURN((PLV_ERROR, "Invalid record name %u\n", rid), false);
		if (r.islast() && trace.size() > 0 && r.next() != trace.top())
			PELOG_ERROR_RETURN((PLV_ERROR, "Invalid loopback %u: \n", rid), false);
		(r.isdir() ? stat->ndir : stat->nfile) ++;

		if (r.isdir() && r.sub())
		{
			trace.push(rid);
			rid = r.sub();
		}
		else if (r.islast())
			rid = 0;
		else
			rid = r.next();
	}
	if (stat->ndir + stat->nfile + stat->nrecy != _records.size())
	{
		PELOG_ERROR_RETURN((PLV_ERROR, "Wild records %u:%u:%u:%u\n",
			(unsigned int)stat->ndir, (unsigned int)stat->nfile, (unsigned int)stat->nrecy, (unsigned int)_records.size()), false);
	}
	return true;
}

bool Root::verifydir(uint32_t pid) const
{
	if (pid == 0 || !_records[pid].isdir())
		PELOG_ERROR_RETURN((PLV_ERROR, "verifydir root not dir %u\n", pid), false);
	const char *lname = NULL;
	for (uint32_t rid = _records[pid].sub(); rid != 0; rid = _records[rid].islast() ? 0 : _records[rid].next())
	{
		const RecordItem &ritem = _records[rid];
		const char *rname = getName(rid);
		if (!*rname)	// only loopback record has no name
			PELOG_ERROR_RETURN((PLV_ERROR, "verifydir no name (%u:%u:%u)\n", pid, rid, ritem.name()), false);
		if (ritem.islast() && ritem.next() != pid)	// last.next() must be parent
			PELOG_ERROR_RETURN((PLV_ERROR, "verifydir no loopback (%u:%u:%u)\n", pid, rid, ritem.next()), false);
		if (lname && pathCmpDp(lname, rname) >= 0)	// name must in ascending order
			PELOG_ERROR_RETURN((PLV_ERROR, "verifydir out of order (%u:%u) %s : %s\n", pid, rid, lname, rname), false);
		lname = rname;
	}
	return true;
}

int Root::startRefresh()
{
	restate.clear();
	restate.resize(1);
	restate.back().rid = 1;
	failstate.clear();
	return 0;
}

// return: 0: finished, >0: one step, <0: error
int Root::refreshStep(int state, Action &action)
{
	// TODO: do some cleanup if state is not OK
	if (state != Aresq::OK)
	{
		if (state == Aresq::NOTFOUND && (action.type == Action::ADDFILE || action.type == Action::MODFILE))
		{
			PELOG_LOG((PLV_ERROR, "File missing, fallback to parent. %s\n", action.name.buf()));
			AuVerify(recordFail(action.name.buf()));
			AuAssert(restate.size() >= 2);
			restate.pop_back();
			restate.back().stage = RefreshIter::INIT;
			restate.back().files.clear();
		}
		else
			return state;
	}

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

			// basic verifications
			bool iterok = rec.isactive() && rec.isdir() &&
				(reiter.stage == RefreshIter::INIT || pathCmpMt(reiter.name, getName(rec)) == 0);
			// verify parent
			if (iterok)
			{
				const RecordItem *lastrec = &rec;
				while (!lastrec->islast())
					lastrec = &_records[lastrec->next()];
				iterok = lastrec->next() == restate[i - 1].rid;
			}
			if (!iterok)
			{
				PELOG_LOG((PLV_ERROR, "Invalid refresh state %d (%s : %s : %s). move back to parent\n",
					(int)i, reiter.path.buf(), reiter.name.buf(), getName(reiter.rid)));
				AuVerify(recordFail(reiter.path.buf()));
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
			AuVerify(rec.isdir() && rec.isactive() && (reiter.rid == 1 || *rec.name(_rname)));
			// build path
			std::vector<const char *> pathparts;
			pathparts.push_back(_localroot.c_str());
			for (size_t i = 1; i < restate.size(); ++i)
				pathparts.push_back(restate[i].name);
			buildPath(pathparts.data(), pathparts.size(), reiter.path);
			// relative path to local root
			abufchar relpath;
			buildPath(pathparts.data() + 1, pathparts.size() - 1, relpath);
			// get dir contents
			if (ListDir(reiter.path, reiter.files) != 0)
			{
				// list dir failed. maybe it has just been deleted
				if (restate.size() <= 1)
					PELOG_LOG((PLV_ERROR, "Root dir listing failed: %s\n", _localroot.c_str()));
				else
				{
					PELOG_LOG((PLV_WARNING, "Dir missing, REDO parent. %s\n", reiter.path.buf()));
					AuVerify(recordFail(reiter.path.buf()));
					reiter.stage = RefreshIter::REDOUPPER;	// just rerun parent dir
				}
				break;
			}
			// perform AresqIgnore
			for (std::vector<FsItem>::iterator i = reiter.files.begin(); i != reiter.files.end(); ++i)
			{
				abufchar filerelpath;
				buildPath(relpath, i->name, filerelpath);
				if (ignore->isignore(filerelpath, i->isdir()))
				{
					PELOG_LOG((PLV_VERBOSE, "File ignored: %s : %s\n", _localroot.c_str(), filerelpath.buf()));
					i->isignore(true);
				}
			}
			reiter.stage = RefreshIter::REMOVE;
			reiter.prog = 0;
			break;
		}

		case RefreshIter::REMOVE:
		{
			// look for next rec file
			if (reiter.prog == 0)
				reiter.prog = rec.sub();
			else	// already has prog, search for it under rec to ensure it is valid
			{
				uint32_t tgtid = rec.sub();
				for (; tgtid != 0; tgtid = _records[tgtid].islast() ? 0 : _records[tgtid].next())
					if (tgtid == reiter.prog)
						break;
				if (tgtid != reiter.prog)	// prog not found, skip to next stage
				{
					reiter.stage = RefreshIter::NEW;
					reiter.prog = 0;
					break;
				}
			}
			size_t fidx = 0;	// current idx in physical files
			// for each rec file
			for (; reiter.prog != 0; reiter.prog = _records[reiter.prog].islast() ? 0 : _records[reiter.prog].next())
			{
				RecordItem &fitem = _records[reiter.prog];
				// look for the matched item in physical files for the rec file
				while (fidx < reiter.files.size() && pathCmpMt(reiter.files[fidx].name, fitem.name(_rname)) < 0)
					++fidx;
				// if not found
				bool isdel = fidx >= reiter.files.size() || pathCmpMt(reiter.files[fidx].name, fitem.name(_rname)) != 0 ||
					reiter.files[fidx].isdir() != fitem.isdir();
				bool ignore = !isdel && reiter.files[fidx].isignore() != fitem.isignore();
				if (isdel || ignore)
				{
					if (isdel && !fitem.isignore())
						PELOG_LOG((PLV_DEBUG, "DEL item detected %s: %s\n", reiter.path.buf(), fitem.name(_rname)));
					// move forward before return, since prog is likely to be deleted
					if (!fitem.islast())
					{
						reiter.prog = fitem.next();
						AuVerify(reiter.prog != 0);
					}
					else
					{
						reiter.stage = RefreshIter::NEW;
						reiter.prog = 0;
					}
					action.type = fitem.isdir() ? Action::DELDIR : Action::DELFILE;
					buildPath(pathAbs2Rel(reiter.path.buf(), _localroot.c_str()), fitem.name(_rname), action.name);
					return 1;
				}
			}
			// no more DELFILE if reach here, move on to next stage
			reiter.stage = RefreshIter::NEW;
			reiter.prog = 0;
			break;
		}

		case RefreshIter::NEW:
		{
			uint32_t fid = rec.sub();
			for (; reiter.prog < reiter.files.size(); ++reiter.prog)	// for each physical file
			{
				for (; fid != 0 && pathCmpMt(_records[fid].name(_rname), reiter.files[reiter.prog].name) < 0;
					fid = _records[fid].islast() ? 0 : _records[fid].next())
					;
				bool found = fid != 0 && pathCmpMt(_records[fid].name(_rname), reiter.files[reiter.prog].name) == 0;
				if (found)
					AuVerify(_records[fid].isdir() == reiter.files[reiter.prog].isdir());
				if (found && !_records[fid].isdir() && (
					_records[fid].sizeChanged(reiter.files[reiter.prog].size) ||
					abs((int64_t)_records[fid].time() - (int64_t)reiter.files[reiter.prog].time) > 10))
				{
					PELOG_LOG((PLV_DEBUG, "MOD item detected %s: %s\n", reiter.path.buf(), reiter.files[reiter.prog].name));
					action.type = Action::MODFILE;
					buildPath(pathAbs2Rel(reiter.path.buf(), _localroot.c_str()), reiter.files[reiter.prog].name, action.name);
					reiter.prog++;	// move forward before return
					return 1;
				}
				else if (!found)
				{
					PELOG_LOG((PLV_DEBUG, "ADD item detected %s: %s\n", reiter.path.buf(), reiter.files[reiter.prog].name));
					action.type = reiter.files[reiter.prog].isdir() ? Action::ADDDIR : Action::ADDFILE;
					buildPath(pathAbs2Rel(reiter.path.buf(), _localroot.c_str()), reiter.files[reiter.prog].name, action.name);
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
			while (reiter.prog != 0 && !_records[reiter.prog].isdir())
				reiter.prog = _records[reiter.prog].islast() ? 0 : _records[reiter.prog].next();
			if (reiter.prog != 0 && _records[reiter.prog].isdir())
			{
				uint32_t recurid = reiter.prog;
				AuVerify(*getName(recurid));
				reiter.prog = _records[reiter.prog].islast() ? 0 : _records[reiter.prog].next();
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
	int res = Aresq::OK;
	// process parents
	size_t baselen = splitPath(dir, dlen);
	uint32_t pid = 1;
	if (baselen > 0 && (res = addDir(dir, baselen, pid, remote)) != Aresq::OK)	// not top level dir, create parents
		return res;
	const char *dirname = baselen == 0 ? dir : dir + baselen + 1;
	size_t nlen = dlen - (dirname - dir);
	// check local
	FindResult dtype = FR_MATCH;
	did = findRecord(pid, dirname, nlen, dtype);
	if (dtype == FR_MATCH && did != 0 && !_records[did].isdir())	// local is a file, error
		PELOG_ERROR_RETURN((PLV_ERROR, "Create dir failed. file exists. %.*s\n", dlen, dir), Aresq::CONFLICT);
	if (dtype == FR_MATCH && did != 0 && _records[did].isdir())	// local already exists
		return Aresq::OK;
	AuVerify((dtype == FR_PRE || dtype == FR_PARENT) && did != 0);
	uint32_t preid = did;
	// local not found. add remote first
	if ((res = remote->addDir(_name.c_str(), std::string(dir, dlen).c_str())) != Aresq::OK)
	{
		if (res != Aresq::DISCONNECTED)
			PELOG_ERROR_RETURN((PLV_ERROR, "Create dir failed. remote error %d. %.*s\n", res, dlen, dir), Aresq::REMOTEERR);
		else
			PELOG_ERROR_RETURN((PLV_TRACE, "Remote disconnected.\n"), Aresq::DISCONNECTED);
	}
	// add local
	std::vector<uint32_t> cids;	// changed ids
	did = allocRec(cids);
	RecordItem &ditem = _records[did];
	ditem.name(allocRName(dirname, nlen));
	ditem.isdir(true);
	ditem.time((uint32_t)getDirTime(_localroot.c_str(), dir, dlen));
	// insert the new record
	cids.push_back(preid);
	if (preid == pid)
	{
		ditem.next(_records[pid].sub() == 0 ? pid : _records[pid].sub());
		ditem.islast(_records[pid].sub() == 0);
		_records[pid].sub(did);
	}
	else
	{
		ditem.next(_records[preid].next());
		ditem.islast(_records[preid].islast());
		_records[preid].islast(false);
		_records[preid].next(did);
	}
	AuAssert(verifydir(pid));
	writeRec(cids);
	PELOG_LOG((PLV_INFO, "DIR ADDed(%u) %s : %.*s\n", did, _localroot.c_str(), dlen, dir));
	return Aresq::OK;
}

// fid: output the target file record id
int Root::addFile(const char *file, size_t flen, uint32_t &fid, Remote *remote)
{
	// TODO: manage history versions
	int res = Aresq::OK;
	bool pendingfail = false;	// error occurred but is allowed to continue
	// process parents
	size_t baselen = splitPath(file, flen);
	uint32_t pid = 1;
	if (baselen > 0 && (res = addDir(file, baselen, pid, remote)) != Aresq::OK)	// not in top level dir, create parents
		return res;
	const char *filename = baselen == 0 ? file : file + baselen + 1;
	size_t nlen = flen - (filename - file);
	// get attr
	uint64_t ftime = 0;
	uint64_t fsize = 0;
	if (getFileAttr(_localroot.c_str(), file, flen, ftime, fsize) != 0)
		PELOG_ERROR_RETURN((PLV_ERROR, "Get file attr failed. %s : %.*s\n", _localroot.c_str(), flen, file), Aresq::NOTFOUND);
	PELOG_LOG((PLV_TRACE, "FILE size %llu time %llu. %s : %.*s\n", fsize, ftime, _localroot.c_str(), flen, file));
	// check local
	FindResult dtype = FR_MATCH;
	fid = findRecord(pid, filename, nlen, dtype);
	if (dtype == FR_MATCH && fid != 0 && _records[fid].isdir())	// local is a dir, error
		PELOG_ERROR_RETURN((PLV_ERROR, "addFile failed. dir exists. %.*s\n", flen, file), Aresq::CONFLICT);
	if (dtype == FR_MATCH && fid != 0 && !_records[fid].isdir() &&
			_records[fid].time() == (uint32_t)ftime && !_records[fid].sizeChanged(fsize))	// local already exists & no change
		return Aresq::OK;
	bool isnew = !(dtype == FR_MATCH && fid != 0 && !_records[fid].isdir());
	AuVerify(fid != 0);
	AuVerify(dtype == FR_MATCH || dtype == FR_PRE || dtype == FR_PARENT);
	uint32_t preid = dtype != FR_MATCH ? fid : 0;
	fid = dtype == FR_MATCH ? fid : 0;
	// add remote first
	if ((res = remote->addFile(_localroot.c_str(), _name.c_str(), std::string(file, flen).c_str())) != Aresq::OK)
	{
		if (res == Aresq::NOTFOUND)
			PELOG_ERROR_RETURN((PLV_ERROR, "addFile failed. file missing %d. %.*s\n", res, flen, file), Aresq::NOTFOUND);
		else if (res == Aresq::FILELOCKED)
		{
			// file locked, record it locally and continue
			pendingfail = true;
			PELOG_LOG((PLV_ERROR, "addFile failed. file locked %d. %.*s\n", res, flen, file));
		}
		else if (res != Aresq::DISCONNECTED)
			PELOG_ERROR_RETURN((PLV_ERROR, "addFile failed. remote error %d. %.*s\n", res, flen, file), Aresq::REMOTEERR);
		else
			PELOG_ERROR_RETURN((PLV_TRACE, "Remote disconnected.\n"), Aresq::DISCONNECTED);
	}
	// add local
	std::vector<uint32_t> cids;	// changed ids
	if (fid == 0)
	{
		AuVerify(preid != 0);
		fid = allocRec(cids);
		if (preid == pid)
		{
			_records[fid].next(_records[pid].sub() == 0 ? pid : _records[pid].sub());
			_records[fid].islast(_records[pid].sub() == 0);
			_records[pid].sub(fid);
		}
		else
		{
			_records[fid].next(_records[preid].next());
			_records[fid].islast(_records[preid].islast());
			_records[preid].islast(false);
			_records[preid].next(fid);
		}
		_records[fid].name(allocRName(filename, nlen));
		cids.push_back(preid);
	}
	cids.push_back(fid);
	RecordItem &fitem = _records[fid];
	fitem.isdir(false);
	fitem.ispending(pendingfail);
	if (!pendingfail)	// update time and size only if not pending_fail
	{
		fitem.time((uint32_t)ftime);
		fitem.size24(fsize);
	}
	AuAssert(verifydir(pid));
	writeRec(cids);
	PELOG_LOG((PLV_INFO, "FILE %s(%u) %s : %.*s\n", isnew ? "ADDed" : "MODed", fid, _localroot.c_str(), flen, file));
	return Aresq::OK;
}

int Root::delDir(const char *dir, size_t dlen, Remote *remote)
{
	// find parent id
	FindResult foundtype = FR_MATCH;
	uint32_t pid = 0;
	uint32_t did = findRecordRoot(dir, dlen, foundtype, pid);
	if (did == 0 || pid == 0 || foundtype != FR_MATCH || !_records[did].isdir())
		PELOG_ERROR_RETURN((PLV_ERROR, "recdir not found %s : %.*s\n", _localroot.c_str(), dlen, dir), Aresq::NOTFOUND);
	return delDir(did, pid, dir, dlen, remote);
}

int Root::delDir(uint32_t rid, uint32_t pid, const char *dir, size_t dlen, Remote *remote)
{
	// TODO: hist
	// TODO: verify physical dir existance
	AuVerify(_records[rid].isdir() && *dir && dlen > 0 && *getName(rid));
	std::vector<uint32_t> cids;	// changed ids
	int res = Aresq::OK;

	// delete sub records
	while (_records[rid].sub())
	{
		uint32_t sid = _records[rid].sub();
		RecordItem &sub = _records[sid];
		abuf<char> subname;
		if (*getName(sid))
			buildPath(dir, dlen, getName(sid), strlen(getName(sid)), subname);
		if (!sub.isdir())	// a regular file
		{
			if ((res = delFile(sid, rid, subname, strlen(subname), remote)) != Aresq::OK)
				return res;
		}
		else if (sub.isdir() && *getName(sid))	// a regular dir
		{
			if ((res = delDir(sid, rid, subname, strlen(subname), remote)) != Aresq::OK)
				return res;
		}
		else	// this should no happen
			AuVerify(false);
	}	// while (_records[rid].sub())	// delete sub records

	// must be an empty dir if reaches here
	// delete remote
	if ((res = remote->delDir(_name.c_str(), std::string(dir, dlen).c_str())) != Aresq::OK)
	{
		if (res != Aresq::DISCONNECTED)
			PELOG_ERROR_RETURN((PLV_ERROR, "Del file failed. remote error %d. %.*s\n", res, dlen, dir), Aresq::REMOTEERR);
		else
			PELOG_ERROR_RETURN((PLV_TRACE, "Remote disconnected.\n"), Aresq::DISCONNECTED);
	}
	// delete record
	RecPtr preptr(this);
	for (preptr.set(pid, RPSUB); preptr() != rid && preptr() != pid && preptr() != 0; preptr.set(preptr(), RPNEXT))
		;	// look for pre
	AuVerify(preptr() == rid);
	cids.push_back(rid);
	cids.push_back(preptr._id);
	// update ptrs
	if (preptr._type == RPSUB)
	{
		preptr(_records[rid].islast() ? 0 : _records[rid].next());
	}
	else
	{
		preptr(_records[rid].next());
		_records[preptr._id].islast(_records[rid].islast());
	}
	// recycle
	recycleRec(rid, cids);

	AuAssert(verifydir(pid));
	writeRec(cids);
	PELOG_LOG((PLV_INFO, "DIR DELed(%u) %s : %.*s\n", rid, _localroot.c_str(), dlen, dir));
	return Aresq::OK;
}

int Root::delFile(const char *filename, size_t flen, Remote *remote)
{
	// TODO: hist
	// TODO: verify physical file existance
	// find parent id
	FindResult foundtype = FR_MATCH;
	uint32_t pid = 0;
	uint32_t fid = findRecordRoot(filename, flen, foundtype, pid);
	if (fid == 0 || pid == 0 || foundtype != FR_MATCH || _records[fid].isdir())
		PELOG_ERROR_RETURN((PLV_ERROR, "recfile not found %s : %.*s\n", _localroot.c_str(), flen, filename), Aresq::NOTFOUND);
	return delFile(fid, pid, filename, flen, remote);
}

int Root::delFile(uint32_t rid, uint32_t pid, const char *filename, size_t flen, Remote *remote)
{
	AuVerify(!_records[rid].isdir() && *filename && *getName(rid));
	std::vector<uint32_t> cids;	// changed ids
	int res = 0;
	// del remote
	if ((res = remote->delFile(_name.c_str(), std::string(filename, flen).c_str())) != Aresq::OK)
	{
		if (res != Aresq::DISCONNECTED)
			PELOG_ERROR_RETURN((PLV_ERROR, "Del file failed. remote error %d. %.*s\n", res, flen, filename), Aresq::REMOTEERR);
		else
			PELOG_ERROR_RETURN((PLV_TRACE, "Remote disconnected.\n"), Aresq::DISCONNECTED);
	}
	// del local
	RecPtr preptr(this);
	for (preptr.set(pid, RPSUB); preptr() != rid && preptr() != pid && preptr() != 0; preptr.set(preptr(), RPNEXT))
		;	// look for pre
	AuVerify(preptr() == rid);
	// detach
	cids.push_back(rid);
	cids.push_back(preptr._id);
	preptr(_records[rid].next());
	if (preptr._id == pid && preptr() == pid)	// rid is pid's only child
		preptr(0);
	// recycle
	recycleRec(rid, cids);

	AuAssert(verifydir(pid));
	writeRec(cids);
	PELOG_LOG((PLV_INFO, "FILE DELed(%u) %s : %.*s\n", rid, _localroot.c_str(), flen, filename));
	return Aresq::OK;
}

// look for the specific name under pid, return rid if found, otherwise pre or parent id
uint32_t Root::findRecord(uint32_t pid, const char *name, size_t namelen, FindResult &restype)
{
	AuVerify(_records.size() >= 2 && namelen > 0 && _records[pid].isdir());
	uint32_t delid = 0, preid = pid;
	restype = FR_MATCH;
	for (uint32_t rid = _records[pid].sub(); rid != 0 && rid != pid; preid = rid, rid = _records[rid].next())
	{
		int cmp = pathCmpDp(name, namelen, getName(rid));
		if (cmp == 0)
			return rid;
		else if (cmp < 0)
			break;
	}
	restype = preid == pid ? FR_PARENT : FR_PRE;
	return preid;
}

uint32_t Root::findRecordRoot(const char *name, size_t namelen, FindResult &restype, uint32_t &pid)
{
	pid = 1;
	restype = FR_MATCH;
	size_t baselen = splitPath(name, namelen);
	if (baselen == 0)	// in root
		return findRecord(pid, name, namelen, restype);
	// look for parent id
	{
		uint32_t tmpid = 0;
		FindResult tmptype = FR_MATCH;
		pid = findRecordRoot(name, baselen, tmptype, tmpid);
		if (pid == 0 || tmptype != FR_MATCH || !_records[pid].isdir())	// parent not found
		{
			restype = FR_NONE;
			pid = 0;
			return 0;
		}
	}
	return findRecord(pid, name + baselen + 1, namelen - baselen - 1, restype);
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
	_records[nid].isactive(true);
	cids.push_back(nid);
	return nid;
}

int Root::eraseName(uint32_t rid)
{
	AuVerify(*getName(rid));
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
	return Aresq::OK;
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
	_records[rid].isactive(false);
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
	PELOG_ERROR_RETURN((PLV_WARNING, "Unsupported action %d\n", action.type), Aresq::NOTIMPLEMENTED);
}