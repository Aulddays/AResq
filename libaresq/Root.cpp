#include "stdafx.h"
#include "Root.h"
#include <stack>
#include <algorithm>
#include <map>
#include <chrono>
#include <ctime>
#include <functional>
#include <cstring>
#include <stdint.h>
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
	if (rootid != -1)
	{
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

	}
#endif
}

int Root::load(int id, const char *name, const char *root, const char *rec_path, bool keephist, AresqIgnore *aresqignore, uint64_t max_file_size)
{
	std::lock_guard<std::mutex> lock(_mutex);
	rootid = -1;
	_name = name;
	_localroot = root;
	recpath = rec_path;
	this->keephist = keephist;
	this->max_file_size = max_file_size;
	ignore = aresqignore;

	if (CreateDir(recpath.c_str()) != 0)
		PELOG_ERROR_RETURN((PLV_ERROR, "Create RECPATH failed %s\n", recpath.c_str()), -1);

#ifndef FRESH_DEBUG
	// verify and recover failed compact status
	if (recoverCompact() != 0)
		PELOG_ERROR_RETURN((PLV_ERROR, "Recover compact failed %s\n", recpath.c_str()), -1);

	FILEGuard fp = NULL;
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
		if (!verifyrec(_records, _rname, &stat))
		{
			PELOG_LOG((PLV_ERROR, "%s: verify loaded failed\n", name));
			AuAssert(false);
			goto ERROR_CLEAR;
		}
		PELOG_LOG((PLV_INFO, "Loaded (%s) file %u, dir %u, recycled %u\n",
			name, stat.nfile, stat.ndir, stat.nrecy));
	}

	rootid = id;

	return 0;

ERROR_CLEAR:
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

bool Root::verifyrec(const std::vector<RecordItem> &records, const std::vector<char> &rname, Root::RootStat *stat) const
{
	RootStat tstat;
	if (!stat)
		stat = &tstat;
	memset(stat, 0, sizeof(*stat));
	if (records.size() < 2 || rname.empty() || rname[0] != 0)
		PELOG_ERROR_RETURN((PLV_ERROR, "Invalid record header\n"), false);

	// recycle list
	stat->nrecy = 1;
	for (uint32_t recid = records[0].next(); recid; recid = records[recid].next())
	{
		if (recid >= records.size() || records[recid].isactive())
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
			rid = records[rid].islast() ? 0 : records[rid].next();
			continue;
		}
		if (rid >= records.size())
			PELOG_ERROR_RETURN((PLV_ERROR, "Invalid record id %u\n", rid), false);
		const RecordItem &r = records[rid];
		if (!r.isactive())
			PELOG_ERROR_RETURN((PLV_ERROR, "Invalid record state %u\n", rid), false);
		if (r.name() >= rname.size())
			PELOG_ERROR_RETURN((PLV_ERROR, "Invalid record name %u\n", rid), false);
		if (r.name() != 0 && memchr(&rname[r.name()], 0, rname.size() - r.name()) == NULL)
			PELOG_ERROR_RETURN((PLV_ERROR, "Unterminated record name %u\n", rid), false);
		if (r.islast() && trace.size() > 0 && r.next() != trace.top())
			PELOG_ERROR_RETURN((PLV_ERROR, "Invalid loopback %u: \n", rid), false);
		if (r.isdir() && r.sub() >= records.size())
			PELOG_ERROR_RETURN((PLV_ERROR, "Invalid sub record %u\n", rid), false);
		if (!r.islast() && r.next() >= records.size())
			PELOG_ERROR_RETURN((PLV_ERROR, "Invalid next record %u\n", rid), false);
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
	if (stat->ndir + stat->nfile + stat->nrecy != records.size())
	{
		PELOG_ERROR_RETURN((PLV_ERROR, "Wild records %u:%u:%u:%u\n",
			(unsigned int)stat->ndir, (unsigned int)stat->nfile, (unsigned int)stat->nrecy, (unsigned int)records.size()), false);
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

int Root::startRefresh(const std::vector<std::string> *initstep, std::unique_lock<std::mutex> &refreshLock)
{
	refreshLock = std::unique_lock<std::mutex>(_refreshMutex, std::try_to_lock);
	if (!refreshLock.owns_lock())
		PELOG_ERROR_RETURN((PLV_ERROR, "Root refresh already running %d:%s\n", rootid, _name.c_str()), Aresq::EINTERNAL);

	std::lock_guard<std::mutex> lock(_mutex);
	restate.clear();
	restate.resize(1);
	restate.back().rid = 1;
	failstate.clear();
	if (initstep)
		reinit = *initstep;
	else
		reinit.clear();
	return Aresq::OK;
}

// Preparing restate for refreshing single path, optionally recuring into sub dirs
// return: OK: finished by simple update, AGAIN: continue with refreshStep, otherwise error
int Root::startRefreshSingle(const char *path, Remote *remote, bool recur, std::unique_lock<std::mutex> &refreshLock)
{
	refreshLock = std::unique_lock<std::mutex>(_refreshMutex);
	std::lock_guard<std::mutex> lock(_mutex);
	int res = Aresq::OK;
	restate.clear();
	reinit.clear();
	failstate.clear();

	// Split the relative path. Empty path means local root.
	std::vector<std::string> parts;
	for (const char *p = path ? path : ""; *p;)
	{
		while (*p == '/')
			++p;
		const char *beg = p;
		while (*p && *p != '/')
			++p;
		if (p != beg)
		{
			parts.emplace_back(beg, p);
			if (parts.back() == "." || parts.back() == "..")
				PELOG_ERROR_RETURN((PLV_ERROR, "startRefreshSingle: Invalid component: %s\n", parts.back().c_str()), Aresq::EPARAM);
		}
	}

	if (parts.empty())	// root dir, just start
	{
		restate.resize(1);
		restate.back().rid = 1;
		restate.back().stage = RefreshIter::INIT;
		restate.back().iterConfig = RefreshIter::NOUPPER | (recur ? 0 : RefreshIter::NORECUR);
		return Aresq::AGAIN;
	}

	// Walk each component and build restate.
	restate.resize(1);	// Add root dir into restate before start
	restate.back().rid = 1;
	restate.back().stage = RefreshIter::RETURN;
	uint32_t pid = 1;
	std::string curpath;
	for (size_t i = 0; i < parts.size(); ++i)
	{
		if (!curpath.empty())
			curpath.push_back('/');
		curpath.append(parts[i]);
		bool islast = i + 1 == parts.size();

		// check record
		FindResult rtype = FR_MATCH;
		uint32_t rid = findRecord(pid, parts[i].c_str(), parts[i].size(), rtype);
		bool rexist = rid != 0 && rtype == FR_MATCH;
		bool rdir = rexist && _records[rid].isdir();
		bool rignore = rexist && _records[rid].isignore();
		// intermediate record ok, move on to next level
		if (!islast && rexist && rdir)
		{
			restate.resize(restate.size() + 1);
			restate.back().rid = rid;
			restate.back().name.scopyFrom(_records[rid].name(_rname));
			restate.back().stage = RefreshIter::RETURN;
			pid = rid;
			continue;
		}

		// record not ok, go on checking physical
		uint64_t ptime = 0, psize = 0;
		bool pdir = false;
		bool pexist = getFileAttr(_localroot.c_str(), curpath.c_str(), curpath.size(), ptime, psize, pdir) == 0;
		bool pignore = pexist && (ignore->isignore(curpath.c_str(), pdir) || max_file_size > 0 && psize > max_file_size);
		// If the physical vanished, delete the stale record and finish.
		if (!pexist)
		{
			if (rexist)
			{
				if (rdir)
					res = delDir(curpath.c_str(), curpath.size(), pignore, keephist && !pignore, false, remote);
				else
					res = delFile(curpath.c_str(), curpath.size(), pignore, keephist && !pignore, false, remote);
				if (res != Aresq::OK && res != Aresq::NOTFOUND)
					return res;
			}
			return Aresq::OK;
		}
		// Type or ignore-state changes are represented as delete + add.
		if (rexist && (rdir != pdir || rignore != pignore))
		{
			if (rdir)
				res = delDir(curpath.c_str(), curpath.size(), rignore, keephist && !rignore && !pignore, false, remote);
			else
				res = delFile(curpath.c_str(), curpath.size(), rignore, keephist && !rignore && !pignore, false, remote);
			if (res != Aresq::OK && res != Aresq::NOTFOUND)
				return res;
			rexist = false;
		}
		// if file, update it and finish.
		if (!pdir)
		{
			// Just call addFile, which internally checks file change
			uint32_t fid = rexist ? rid : 0;
			res = addFile(curpath.c_str(), curpath.size(), pignore, keephist && !pignore, fid, remote);
			return res;
		}
		// physical is dir, create it if not exist
		if (!rexist)
		{
			uint32_t did = 0;
			res = addDir(curpath.c_str(), curpath.size(), pignore, did, remote);
			if (res != Aresq::OK)
				return res;
			rexist = true;
			rdir = true;
			rid = did;
		}
		if (pignore)
			return Aresq::OK;

		// all set, update restate
		restate.resize(restate.size() + 1);
		restate.back().rid = rid;
		restate.back().name.scopyFrom(_records[rid].name(_rname));
		restate.back().stage = islast ? RefreshIter::INIT : RefreshIter::RETURN;
		if (!islast)	// if intermediate dir, just move on to next level
		{
			pid = rid;
			continue;
		}

		AuAssert(restate.size() == parts.size() + 1 && restate.back().rid != 0 &&
			pexist && !pignore && pdir && rexist && rdir);
		restate.back().iterConfig = RefreshIter::NOUPPER | (recur ? 0 : RefreshIter::NORECUR);
		// final level, restate prepared, we are now ready to perform the real refresh. return AGAIN
		return Aresq::AGAIN;
	}

	return Aresq::OK;
}

// return: OK: finished, AGAIN: one step, other: error
int Root::refreshStep(int state, Action &action)
{
	std::lock_guard<std::mutex> lock(_mutex);
	const auto tmstart = std::chrono::steady_clock::now();
	size_t opnum = 0;

	// Record dir refresh time, and lazy flush using ResGuard.
	std::vector<uint32_t> updatedcids;
	ResGuard<std::vector<uint32_t>, std::function<void(std::vector<uint32_t> *)>> updateguard(
		&updatedcids, [this](std::vector<uint32_t> *cids) {
			if (!cids->empty())
				writeRec(*cids);
	});

	// TODO: do some cleanup if state is not OK
	if (state != Aresq::OK)
	{
		if (state == Aresq::NOTFOUND && (action.type == Action::ADDFILE || action.type == Action::MODFILE))
		{
			PELOG_LOG((PLV_ERROR, "File missing, fallback to parent. %s\n", action.name.buf()));
			AuVerify(recordFail(action.name.buf()));
			AuAssert(restate.size() >= 1);
			if (restate.size() >= 2 && !(restate.back().noupper()))	// ==1 => at root, just try root again
				restate.pop_back();
			restate.back().stage = RefreshIter::INIT;
			restate.back().files.clear();
		}
		else
			return state;
	}

	action.type = Action::NONE;
	action.isignore = false;
	action.keephist = false;
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
		if (opnum > 500 || opnum > 0 && reiter.stage == RefreshIter::INIT && reiter.rid != 1 && reinit.empty())
		{
			opnum = 0;
			const auto tmnow = std::chrono::steady_clock::now();
			const std::chrono::duration<double> tmcost = tmnow - tmstart;
			if (tmcost.count() > 1)
			{
				action.type = Action::BREAK;	// take a break once in a while
				return Aresq::AGAIN;
			}
		}
		++opnum;
		switch (reiter.stage)
		{
		case RefreshIter::INIT:
		{
			//reiter.name.scopyFrom(rec.name(_rname));	// the name should have been stored on RECUR
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
					PELOG_ERROR_RETURN((PLV_ERROR, "Root dir listing failed: %s\n", _localroot.c_str()), -1);
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
				if (ignore->isignore(filerelpath, i->isdir()) || max_file_size > 0 && i->size > max_file_size)
				{
					//PELOG_LOG((PLV_DEBUG, "File ignored: %s : %s\n", _localroot.c_str(), filerelpath.buf()));
					i->isignore(true);
				}
			}
			// perform progress restore based on `reinit`, by directly skipping to RECUR
			// reinit.size() >= restate.size() => have deeper steps to restore
			// !reinit[restate.size() - 1].empty() => have not restored (restate will be cleared to avoid renentrance on error)
			if (reinit.size() >= restate.size() && !reinit[restate.size() - 1].empty())
			{
				// look for the step in record
				size_t prog = rec.sub(), ifile = 0;
				int cmp = 1;
				while (prog != 0 && (!_records[prog].isdir() || _records[prog].isignore() ||
						(cmp = pathCmpMt(_records[prog].name(_rname), reinit[restate.size() - 1].c_str())) < 0))
					prog = _records[prog].islast() ? 0 : _records[prog].next();
				if (cmp != 0)
					prog = 0;
				// look for the step in physical files
				cmp = 1;
				while (prog != 0 && ifile < reiter.files.size() &&
						(!reiter.files[ifile].isdir() || reiter.files[ifile].isignore() ||
						(cmp = pathCmpMt(reiter.files[ifile].name, reinit[restate.size() - 1].c_str())) < 0))
					++ifile;
				if (cmp != 0)
					ifile = -1;
				if (prog != 0 && ifile != (decltype(ifile))-1)	// step item found, recur into it
				{
					reiter.stage = RefreshIter::RECUR;
					reiter.prog = prog;
					reinit[restate.size() - 1].clear();	// clear current step, to avoid reentrance on error (DOUPPER)
					if (restate.size() >= reinit.size())	// if have reached the deepest level
						reinit.clear();
					break;
				}
				PELOG_LOG((PLV_WARNING, "Refresh saved step not found %s\n", reinit[restate.size() - 1].c_str()));
				reinit.clear();
				break;
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
				++opnum;
				RecordItem &fitem = _records[reiter.prog];
				// look for the matched item in physical files for the rec file
				while (fidx < reiter.files.size() && pathCmpMt(reiter.files[fidx].name, fitem.name(_rname)) < 0)
					++fidx;
				// if not found
				bool isdel = fidx >= reiter.files.size() || pathCmpMt(reiter.files[fidx].name, fitem.name(_rname)) != 0 ||
					reiter.files[fidx].isdir() != fitem.isdir();
				bool ignorechange = !isdel && reiter.files[fidx].isignore() != fitem.isignore();
				if (isdel || ignorechange)
				{
					if (isdel && !fitem.isignore())
						PELOG_LOG((PLV_DEBUG, "DEL item detected %s: %s\n", reiter.path.buf(), fitem.name(_rname)));
					else if (!isdel && reiter.files[fidx].isignore())
						PELOG_LOG((PLV_DEBUG, "IGNORE del item detected %s: %s\n", reiter.path.buf(), fitem.name(_rname)));
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
					action.isignore = !isdel && reiter.files[fidx].isignore();
					action.keephist = keephist && !action.isignore && !fitem.isignore();
					return Aresq::AGAIN;
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
				++opnum;
				for (; fid != 0 && pathCmpMt(_records[fid].name(_rname), reiter.files[reiter.prog].name) < 0;
					fid = _records[fid].islast() ? 0 : _records[fid].next())
					;
				bool found = fid != 0 && pathCmpMt(_records[fid].name(_rname), reiter.files[reiter.prog].name) == 0;
				if (found)
					AuVerify(_records[fid].isdir() == reiter.files[reiter.prog].isdir());
				if (found && !_records[fid].isdir() && !_records[fid].isignore() && (
					_records[fid].sizeChanged(reiter.files[reiter.prog].size) ||
					_records[fid].timeChanged(reiter.files[reiter.prog].time)))
				{
					PELOG_LOG((PLV_DEBUG, "MOD item detected %s: %s\n", reiter.path.buf(), reiter.files[reiter.prog].name.buf()));
					action.type = Action::MODFILE;
					buildPath(pathAbs2Rel(reiter.path.buf(), _localroot.c_str()), reiter.files[reiter.prog].name, action.name);
					action.keephist = keephist;
					reiter.prog++;	// move forward before return
					return Aresq::AGAIN;
				}
				else if (!found)
				{
					PELOG_LOG((PLV_DEBUG, "%s item detected %s: %s\n",
						reiter.files[reiter.prog].isignore() ? "IGNORE" : "ADD", reiter.path.buf(), reiter.files[reiter.prog].name.buf()));
					action.type = reiter.files[reiter.prog].isdir() ? Action::ADDDIR : Action::ADDFILE;
					buildPath(pathAbs2Rel(reiter.path.buf(), _localroot.c_str()), reiter.files[reiter.prog].name, action.name);
					action.isignore = reiter.files[reiter.prog].isignore();
					reiter.prog++;	// move forward before return
					return Aresq::AGAIN;
				}
			}

			rec.time((uint32_t)time64(NULL));	// dir refresh finished, record the time
			updatedcids.push_back(reiter.rid);	// lazy flush
			reiter.stage = RefreshIter::RECUR;
			reiter.prog = 0;
			break;
		}

		case RefreshIter::RECUR:
			if (reiter.norecur())
			{
				reiter.stage = RefreshIter::RETURN;
				break;
			}
			if (reiter.prog == 0)
				reiter.prog = rec.sub();
			while (reiter.prog != 0 && (!_records[reiter.prog].isdir() || _records[reiter.prog].isignore()))
				reiter.prog = _records[reiter.prog].islast() ? 0 : _records[reiter.prog].next();
			if (reiter.prog != 0 && _records[reiter.prog].isdir() && !_records[reiter.prog].isignore())
			{
				++opnum;
				uint32_t recurid = reiter.prog;
				AuVerify(*getName(recurid));
				reiter.prog = _records[reiter.prog].islast() ? 0 : _records[reiter.prog].next();
				if (reiter.prog == 0)	// this is the last dir to recurse
					reiter.stage = RefreshIter::RETURN;
				// restate.resize() INVALIDATES reiter. should break ASAP
				restate.resize(restate.size() + 1);
				restate.back().rid = recurid;
				restate.back().name.scopyFrom(_records[recurid].name(_rname));
				break;
			}
			reiter.stage = RefreshIter::RETURN;
			break;

		case RefreshIter::REDOUPPER:		// go back to parent dir and run again
			if (reiter.noupper())
				reiter.stage = RefreshIter::RETURN;
			else if (restate.size() <= 1)
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
		}	// switch (reiter.stage)
		continue;	// should move to next round after `switch`
	}	// while (true) // running
	return 0;
}

int Root::refreshSave(std::vector<std::string> *step)
{
	std::lock_guard<std::mutex> lock(_mutex);
	step->clear();
	for (size_t i = 1; i < restate.size(); ++i)
		step->emplace_back(restate[i].name);
	return 0;
}

// did: output the target directory record id
int Root::addDir(const char *dir, size_t dlen, bool isignore, uint32_t &did, Remote *remote)
{
	int res = Aresq::OK;
	// process parents
	size_t parentlen = pathDirLen(dir, dlen);
	uint32_t pid = 1;	// default to top dir if parentlen == 0
	if (parentlen > 0 && (res = addDir(dir, parentlen, false, pid, remote)) != Aresq::OK)	// not top level dir, create parents
		return res;
	const char *dirname = parentlen == 0 ? dir : dir + parentlen + 1;
	size_t nlen = dlen - (dirname - dir);
	// check local
	FindResult dtype = FR_MATCH;
	did = findRecord(pid, dirname, nlen, dtype);
	if (dtype == FR_MATCH && isignore != _records[did].isignore())
		PELOG_ERROR_RETURN((PLV_ERROR, "Create dir failed. ignore mismatch. %.*s\n", dlen, dir), Aresq::CONFLICT);
	if (dtype == FR_MATCH && isignore)
		return Aresq::OK;
	if (dtype == FR_MATCH && did != 0 && !_records[did].isdir())	// local is a file, error
		PELOG_ERROR_RETURN((PLV_ERROR, "Create dir failed. file exists. %.*s\n", dlen, dir), Aresq::CONFLICT);
	if (dtype == FR_MATCH && did != 0 && _records[did].isdir())	// local already exists
		return Aresq::OK;
	AuVerify((dtype == FR_PRE || dtype == FR_PARENT) && did != 0);
	uint32_t preid = did;
	// local not found. add remote first
	if (!isignore && (res = remote->addDir(_name.c_str(), std::string(dir, dlen).c_str())) != Aresq::OK)
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
	ditem.time(0);	// dir.time is last refresh time, which will be updated when refresh finishes
	ditem.isignore(isignore);
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
	PELOG_LOG((PLV_INFO, "DIR %s(%u) %s : %.*s\n", isignore ? "IGNOREd" : "ADDed",  did, _localroot.c_str(), dlen, dir));
	return Aresq::OK;
}

// fid: output the target file record id
int Root::addFile(const char *file, size_t flen, bool isignore, bool keephist, uint32_t &fid, Remote *remote)
{
	int res = Aresq::OK;
	bool pendingfail = false;	// error occurred but is allowed to continue
	// get attr
	uint64_t ftime = 0;
	uint64_t fsize = 0;
	bool isdir_dummy = false;
	if (!isignore && getFileAttr(_localroot.c_str(), file, flen, ftime, fsize, isdir_dummy) != 0)
		PELOG_ERROR_RETURN((PLV_ERROR, "addFile Get file attr failed. %s : %.*s\n", _localroot.c_str(), flen, file), Aresq::NOTFOUND);
	if (isdir_dummy)
		PELOG_ERROR_RETURN((PLV_ERROR, "addFile failed. isdir. %s : %.*s\n", _localroot.c_str(), flen, file), Aresq::NOTFOUND);
	bool isignoreSize = max_file_size > 0 && fsize > max_file_size;
	PELOG_LOG((PLV_TRACE, "FILE size %llu time %llu. %s : %.*s\n", fsize, ftime, _localroot.c_str(), flen, file));
	// process parents
	size_t parentlen = pathDirLen(file, flen);
	uint32_t pid = 1;	// default to top dir if parentlen == 0
	if (parentlen > 0 && (res = addDir(file, parentlen, false, pid, remote)) != Aresq::OK)	// not in top level dir, create parents
		return res;
	const char *filename = parentlen == 0 ? file : file + parentlen + 1;
	size_t nlen = flen - (filename - file);
	// check local
	FindResult dtype = FR_MATCH;
	fid = findRecord(pid, filename, nlen, dtype);
	if (isignoreSize && !isignore)
	{
		// isignoreSize && !isignore: should be from Monitor, where the file size was not tested
		// fully update the ignore record requires some housekeeping, which is implemented in full/single refresh
		// Just skip the request for now and return OK, and a parent refresh will soon
		// be triggered, where the `isignore` housekeeping will be taken care
		if (dtype != FR_MATCH || !_records[fid].isignore())
			PELOG_LOG((PLV_VERBOSE, "addFile ignore by size. %.*s\n", flen, file));
		return Aresq::OK;
	}
	if (dtype == FR_MATCH && isignore != _records[fid].isignore())
		PELOG_ERROR_RETURN((PLV_ERROR, "addFile failed. ignore mismatch. %.*s\n", flen, file), Aresq::CONFLICT);
	if (dtype == FR_MATCH && isignore)
		return Aresq::OK;
	if (dtype == FR_MATCH && fid != 0 && _records[fid].isdir())	// local is a dir, error
		PELOG_ERROR_RETURN((PLV_ERROR, "addFile failed. dir exists. %.*s\n", flen, file), Aresq::CONFLICT);
	if (dtype == FR_MATCH && fid != 0 && !_records[fid].isdir() &&
			!_records[fid].timeChanged((uint32_t)ftime) && !_records[fid].sizeChanged(fsize))	// local already exists & no change
		PELOG_ERROR_RETURN((PLV_VERBOSE, "addFile Already up to date. %.*s\n", flen, file), Aresq::OK);
	bool isnew = !(dtype == FR_MATCH && fid != 0 && !_records[fid].isdir());
	AuVerify(fid != 0);
	AuVerify(dtype == FR_MATCH || dtype == FR_PRE || dtype == FR_PARENT);
	uint32_t preid = dtype != FR_MATCH ? fid : 0;
	fid = dtype == FR_MATCH ? fid : 0;
	// hist
	if (keephist)
		remote->putHist(_name.c_str(), std::string(file, flen).c_str());
	// add remote first
	if (!isignore && (res = remote->addFile(_localroot.c_str(), _name.c_str(), std::string(file, flen).c_str())) != Aresq::OK)
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
		_records[fid].isignore(isignore);
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
	PELOG_LOG((PLV_INFO, "FILE %s(%u) %s : %.*s\n",
		isignore ? "IGNOREd" : (isnew ? "ADDed" : "MODed"), fid, _localroot.c_str(), flen, file));
	return Aresq::OK;
}

int Root::delDir(const char *dir, size_t dlen, bool isignore, bool keephist, bool noremote, Remote *remote)
{
	// find parent id
	FindResult foundtype = FR_MATCH;
	uint32_t pid = 0;
	uint32_t did = findRecordRoot(dir, dlen, foundtype, pid);
	if (did == 0 || pid == 0 || foundtype != FR_MATCH || !_records[did].isdir())
		PELOG_ERROR_RETURN((PLV_ERROR, "recdir not found %s : %.*s\n", _localroot.c_str(), dlen, dir), Aresq::NOTFOUND);
	return delDir(did, pid, dir, dlen, isignore, keephist, noremote, remote);
}

int Root::delDir(uint32_t rid, uint32_t pid, const char *dir, size_t dlen, bool isignore, bool keephist, bool noremote, Remote *remote)
{
	// TODO: verify physical dir existance
	AuVerify(_records[rid].isdir() && *dir && dlen > 0 && *getName(rid));
	std::vector<uint32_t> cids;	// changed ids
	int res = Aresq::OK;

	// hist
	if (keephist && (res = remote->putHist(_name.c_str(), std::string(dir, dlen).c_str())) != Aresq::OK)
	{
		if (res != Aresq::DISCONNECTED)
			PELOG_ERROR_RETURN((PLV_ERROR, "Del hist file failed. remote error %d. %.*s\n", res, dlen, dir), Aresq::REMOTEERR);
		else
			PELOG_ERROR_RETURN((PLV_TRACE, "Remote disconnected.\n"), Aresq::DISCONNECTED);
	}
	noremote = noremote || keephist;

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
			if ((res = delFile(sid, rid, subname, strlen(subname), isignore, false, noremote, remote)) != Aresq::OK)
				return res;
		}
		else if (sub.isdir() && *getName(sid))	// a regular dir
		{
			if ((res = delDir(sid, rid, subname, strlen(subname), isignore, false, noremote, remote)) != Aresq::OK)
				return res;
		}
		else	// this should no happen
			AuVerify(false);
	}	// while (_records[rid].sub())	// delete sub records

	// must be an empty dir if reaches here
	// delete remote
	if (!noremote && !_records[rid].isignore() && (res = remote->delDir(_name.c_str(), std::string(dir, dlen).c_str())) != Aresq::OK)
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
	AuAssert(verifydir(pid));

	recycleRec(rid, cids);	// recycle
	writeRec(cids);
	PELOG_LOG((PLV_INFO, "DIR DELed(%u) %s : %.*s\n", rid, _localroot.c_str(), dlen, dir));
	return Aresq::OK;
}

int Root::delFile(const char *filename, size_t flen, bool isignore, bool keephist, bool noremote, Remote *remote)
{
	// TODO: verify physical file existance? maybe, with isignore
	// find parent id
	FindResult foundtype = FR_MATCH;
	uint32_t pid = 0;
	uint32_t fid = findRecordRoot(filename, flen, foundtype, pid);
	if (fid == 0 || pid == 0 || foundtype != FR_MATCH || _records[fid].isdir())
		PELOG_ERROR_RETURN((PLV_ERROR, "recfile not found %s : %.*s\n", _localroot.c_str(), flen, filename), Aresq::NOTFOUND);
	return delFile(fid, pid, filename, flen, isignore, keephist, noremote, remote);
}

int Root::delFile(uint32_t rid, uint32_t pid, const char *filename, size_t flen, bool isignore, bool keephist, bool noremote, Remote *remote)
{
	AuVerify(!_records[rid].isdir() && *filename && *getName(rid));
	std::vector<uint32_t> cids;	// changed ids
	int res = 0;
	// del remote
	if (keephist && (res = remote->putHist(_name.c_str(), std::string(filename, flen).c_str())) != Aresq::OK ||
		!noremote && !_records[rid].isignore() && (res = remote->delFile(_name.c_str(), std::string(filename, flen).c_str())) != Aresq::OK)
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
	AuAssert(verifydir(pid));

	recycleRec(rid, cids);	// recycle
	writeRec(cids);
	PELOG_LOG((PLV_INFO, "FILE DELed(%u) %s : %.*s\n", rid, _localroot.c_str(), flen, filename));
	return Aresq::OK;
}

int Root::rename(const char *src, const char *dst, Remote *remote)
{
	int res = Aresq::OK;

	// Verify local records
	size_t srclen = strlen(src);
	size_t dstlen = strlen(dst);
	// Find source record
	FindResult foundtype = FR_MATCH;
	uint32_t spid = 0;
	uint32_t sid = findRecordRoot(src, srclen, foundtype, spid);
	if (sid == 0 || spid == 0 || foundtype != FR_MATCH)
		PELOG_ERROR_RETURN((PLV_ERROR, "rename: src not found %d : %.*s\n", rootid, srclen, src), Aresq::NOTFOUND);
	// Dst record
	uint32_t dpid = 0;
	uint32_t did = findRecordRoot(dst, dstlen, foundtype, dpid);
	if (did != 0 && foundtype == FR_MATCH)
		PELOG_ERROR_RETURN((PLV_ERROR, "rename: dst already exists %d : %.*s\n", rootid, dstlen, dst), Aresq::CONFLICT);
	
	// prepare dst parent dir, if not in top level
	size_t dparentlen = pathDirLen(dst, dstlen);
	const char *dname = dparentlen == 0 ? dst : dst + dparentlen + 1;
	size_t dnamelen = dstlen - (dname - dst);
	dpid = 1;	// default to top dir if dparentlen == 0
	if (dparentlen > 0 && (res = addDir(dst, dparentlen, false, dpid, remote)) != Aresq::OK)
		return res;

	// Remote move
	if (keephist)	// recycle remote dst
		remote->putHist(_name.c_str(), dst);
	if ((res = remote->moveFile(_name.c_str(), src, dst, true)) != Aresq::OK)
	{
		if (res != Aresq::DISCONNECTED)
			PELOG_ERROR_RETURN((PLV_ERROR, "rename: remote failed (%d) %d : %s -> %s\n", res, rootid, src, dst), Aresq::REMOTEERR);
		else
			PELOG_ERROR_RETURN((PLV_TRACE, "Remote disconnected.\n"), Aresq::DISCONNECTED);
	}

	// Update local records: remove (detach) + add (attach)
	std::vector<uint32_t> cids;
	RecPtr preptr(this);
	// Detach sid from spid
	for (preptr.set(spid, RPSUB); preptr() != sid && preptr() != spid && preptr() != 0; preptr.set(preptr(), RPNEXT))
		;
	AuVerify(preptr() == sid);
	cids.push_back(sid);
	cids.push_back(preptr._id);
	if (preptr._type == RPSUB)
		preptr(_records[sid].islast() ? 0 : _records[sid].next());
	else
	{
		preptr(_records[sid].next());
		_records[preptr._id].islast(_records[sid].islast());
	}
	AuAssert(verifydir(spid));
	// Update name in record
	const char *sname = baseName(src, srclen);
	size_t snamelen = srclen - (sname - src);
	if (snamelen != dnamelen || memcmp(sname, dname, snamelen) != 0)
	{
		eraseName(sid);
		_records[sid].name(allocRName(dname, (uint32_t)dnamelen));
	}
	// Reattach rid to new destination parent
	uint32_t dpreid = findRecord(dpid, dname, dnamelen, foundtype);
	AuAssert(dpreid != 0 && foundtype != FR_MATCH);
	// Attach rid to dpid
	cids.push_back(dpid);
	if (dpreid == dpid)
	{
		_records[sid].next(_records[dpid].sub() == 0 ? dpid : _records[dpid].sub());
		_records[sid].islast(_records[dpid].sub() == 0);
		_records[dpid].sub(sid);
	}
	else
	{
		cids.push_back(dpreid);
		_records[sid].next(_records[dpreid].next());
		_records[sid].islast(_records[dpreid].islast());
		_records[dpreid].islast(false);
		_records[dpreid].next(sid);
	}
	AuAssert(verifydir(dpid));

	writeRec(cids);
	PELOG_LOG((PLV_INFO, "RENAMEd (%d) %s : %.*s -> %.*s\n", rootid, _localroot.c_str(), srclen, src, dstlen, dst));
	return Aresq::OK;
}

// look for the specific name under pid, return rid if found, otherwise pre or parent id
uint32_t Root::findRecord(uint32_t pid, const char *name, size_t namelen, FindResult &restype) const
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

uint32_t Root::findRecordRoot(const char *name, size_t namelen, FindResult &restype, uint32_t &pid) const
{
	pid = 1;
	restype = FR_MATCH;
	size_t parentlen = pathDirLen(name, namelen);
	if (parentlen == 0)	// in root
		return findRecord(pid, name, namelen, restype);
	// look for parent id
	{
		uint32_t tmpid = 0;
		FindResult tmptype = FR_MATCH;
		pid = findRecordRoot(name, parentlen, tmptype, tmpid);
		if (pid == 0 || tmptype != FR_MATCH || !_records[pid].isdir())	// parent not found
		{
			restype = FR_NONE;
			pid = 0;
			return 0;
		}
	}
	return findRecord(pid, name + parentlen + 1, namelen - parentlen - 1, restype);
}

uint32_t Root::getRecordTime(const char *path) const
{
	std::lock_guard<std::mutex> lock(_mutex);
	if (!path || !*path)
		return _records.size() > 1 && _records[1].isactive() ? _records[1].time() : 0;

	FindResult restype = FR_NONE;
	uint32_t pid = 0;
	uint32_t rid = findRecordRoot(path, strlen(path), restype, pid);
	if (rid == 0 || restype != FR_MATCH || !_records[rid].isactive())
		return 0;
	return _records[rid].time();
}

int Root::getRecordType(const char *path) const	// -1: not found, 0: file, 1: dir
{
	std::lock_guard<std::mutex> lock(_mutex);
	if (!path || !*path)
		return _records.size() > 1 && _records[1].isactive() ? 1 : -1;
	FindResult restype = FR_NONE;
	uint32_t pid = 0;
	uint32_t rid = findRecordRoot(path, strlen(path), restype, pid);
	if (rid == 0 || restype != FR_MATCH || !_records[rid].isactive())
		return -1;
	return _records[rid].isdir() ? 1 : 0;
}

// File OP helpers
// Write and force data to disk before the file participates in compact commit.
static int writeAllFile(const char *dir, const char *name, const void *data, size_t size)
{
	FILEGuard fp = OpenFile(dir, name, _NCT("wb"));
	if (!fp)
		PELOG_ERROR_RETURN((PLV_ERROR, "Open file to write failed %s/%s\n", dir, name), -1);
	if (size > 0 && fwrite(data, 1, size, fp) != size)
		PELOG_ERROR_RETURN((PLV_ERROR, "Write file failed %s/%s\n", dir, name), -1);
	if (FlushFile(fp) != 0)
		PELOG_ERROR_RETURN((PLV_ERROR, "Flush file failed %s/%s\n", dir, name), -1);
	fp.release();
	return 0;
}

static int readRecordFile(const char *dir, const char *name, std::vector<RecordItem> &records)
{
	FILEGuard fp = OpenFile(dir, name, _NCT("rb"));
	if (!fp)
		return -1;
	size_t fsize = (size_t)getFileSize(fp);
	if (fsize % sizeof(RecordItem) != 0)
		return -1;
	records.resize(fsize / sizeof(RecordItem));
	if (!records.empty() && fread(records.data(), sizeof(RecordItem), records.size(), fp) != records.size())
		return -1;
	return 0;
}

static int readNameFile(const char *dir, const char *name, std::vector<char> &rname)
{
	FILEGuard fp = OpenFile(dir, name, _NCT("rb"));
	if (!fp)
		return -1;
	size_t fsize = (size_t)getFileSize(fp);
	rname.resize(fsize);
	if (fsize > 0 && fread(rname.data(), 1, fsize, fp) != fsize)
		return -1;
	return 0;
}

static int safeRename(const char *dir, const char *oldname, const char *newname)
{
	if (FileExists(dir, newname) && RemoveFile(dir, newname) != 0)
		PELOG_ERROR_RETURN((PLV_ERROR, "Remove target failed %s/%s\n", dir, newname), -1);
	if (RenameFile(dir, oldname, newname) != 0)
		PELOG_ERROR_RETURN((PLV_ERROR, "Rename failed %s/%s -> %s\n", dir, oldname, newname), -1);
	return 0;
}

// compact the data records by reclaim deleted content in names
// currently recycled records are not compacted: much more complicated, invalidated rids may crash refresh process
int Root::compact(uint32_t limit)
{
	std::lock_guard<std::mutex> lock(_mutex);
	if (_records.size() < 2 || _rname.empty())
		PELOG_ERROR_RETURN((PLV_ERROR, "compact (%s): root not loaded\n", _name.c_str()), Aresq::EINTERNAL);

	// First pass: estimate reclaimable bytes without building a new buffer.
	uint64_t usedSize = 1;
	for (uint32_t rid = 2; rid < _records.size(); ++rid)
	{
		if (!_records[rid].isactive() || _records[rid].name() == 0)
			continue;
		usedSize += strlen(getName(rid)) + 1;
	}
	uint64_t freeSize = _rname.size() > usedSize ? _rname.size() - usedSize : 0;
	if (freeSize <= limit)
	{
		PELOG_LOG((PLV_VERBOSE, "compact (%s): skip, free %u <= limit %u\n", _name.c_str(), (unsigned int)freeSize, (unsigned int)limit));
		return Aresq::OK;
	}

	// Second pass: repack live names and rewrite record offsets.
	std::vector<RecordItem> newRecords = _records;
	std::vector<char> newRName;
	newRName.resize(1);
	newRName[0] = 0;
	for (uint32_t rid = 2; rid < newRecords.size(); ++rid)
	{
		if (!_records[rid].isactive() || _records[rid].name() == 0)
		{
			newRecords[rid].name(0u);
			continue;
		}
		const char *name = getName(rid);
		size_t nlen = strlen(name);
		uint32_t base = (uint32_t)newRName.size();
		AuVerify(base <= UINT32_MAX && nlen < UINT32_MAX - base);
		newRName.resize(base + nlen + 1);
		memcpy(&newRName[base], name, nlen + 1);
		newRecords[rid].name(base);
	}
	if (!verifyrec(newRecords, newRName))
		PELOG_ERROR_RETURN((PLV_ERROR, "compact: verify compacted records failed\n"), Aresq::EINTERNAL);
	if (newRName.size() >= _rname.size())
	{
		PELOG_LOG((PLV_INFO, "compact: skip, rname %u -> %u\n", (unsigned int)_rname.size(), (unsigned int)newRName.size()));
		return Aresq::OK;
	}

#ifndef DRY_RUN
	// New data transaction process, to ensure atomicity and recoverability:
	//   new data (write)-> record.compact.tmp, rname.compact.tmp
	//   (create) compact.commit, the transaction meta file
	//   old data (rename)-> record.compact.bak and rname.compact.bak;
	//   new data (rename) record.compact.tmp -> record, rname.compact.tmp -> rname
	//   (remove) compact.commit, then old data
	static const char *commit = "compact.commit";
	static const char *recordTmp = "record.compact.tmp";
	static const char *rnameTmp = "rname.compact.tmp";
	static const char *recordBak = "record.compact.bak";
	static const char *rnameBak = "rname.compact.bak";
	if (recoverCompact() != 0)
		PELOG_ERROR_RETURN((PLV_ERROR, "compact: recover previous compact failed\n"), Aresq::EINTERNAL);
	if (writeAllFile(recpath.c_str(), recordTmp, newRecords.data(), newRecords.size() * sizeof(newRecords[0])) != 0 ||
		writeAllFile(recpath.c_str(), rnameTmp, newRName.data(), newRName.size()) != 0)
		PELOG_ERROR_RETURN((PLV_ERROR, "compact: write tmp failed\n"), Aresq::EINTERNAL);
	const char commitContent[] = "compact\n";
	if (writeAllFile(recpath.c_str(), commit, commitContent, sizeof(commitContent) - 1) != 0)
		PELOG_ERROR_RETURN((PLV_ERROR, "compact: write commit failed\n"), Aresq::EINTERNAL);
	if (safeRename(recpath.c_str(), "record", recordBak) != 0 ||
		safeRename(recpath.c_str(), "rname", rnameBak) != 0)
		PELOG_ERROR_RETURN((PLV_ERROR, "compact: backup current failed\n"), Aresq::EINTERNAL);
	if (safeRename(recpath.c_str(), recordTmp, "record") != 0 ||
		safeRename(recpath.c_str(), rnameTmp, "rname") != 0)
		PELOG_ERROR_RETURN((PLV_ERROR, "compact: install compacted files failed\n"), Aresq::EINTERNAL);
	RemoveFile(recpath.c_str(), commit);
	if (FileExists(recpath.c_str(), recordBak))
		RemoveFile(recpath.c_str(), recordBak);
	if (FileExists(recpath.c_str(), rnameBak))
		RemoveFile(recpath.c_str(), rnameBak);
#endif

	PELOG_LOG((PLV_INFO, "compact (%s): rname %u -> %u\n", _name.c_str(), (unsigned int)_rname.size(), (unsigned int)newRName.size()));
	_records.swap(newRecords);
	_rname.swap(newRName);
	return Aresq::OK;
}

// detect and cleanup any failed compact transaction
int Root::recoverCompact()
{
#ifndef DRY_RUN
	// compact.commit means a previous compact may have stopped mid-transaction.
	static const char *commit = "compact.commit";
	static const char *recordTmp = "record.compact.tmp";
	static const char *rnameTmp = "rname.compact.tmp";
	static const char *recordBak = "record.compact.bak";
	static const char *rnameBak = "rname.compact.bak";

	// If no failed active transaction; just discard the stale temp files, if any
	if (!FileExists(recpath.c_str(), commit))
	{
		if (FileExists(recpath.c_str(), recordTmp))
			RemoveFile(recpath.c_str(), recordTmp);
		if (FileExists(recpath.c_str(), rnameTmp))
			RemoveFile(recpath.c_str(), rnameTmp);
		if (FileExists(recpath.c_str(), recordBak))
			RemoveFile(recpath.c_str(), recordBak);
		if (FileExists(recpath.c_str(), rnameBak))
			RemoveFile(recpath.c_str(), rnameBak);
		return 0;
	}

	// Active failed tranction found, roll back the old data and clean up temp files
	if (FileExists(recpath.c_str(), recordBak))
	{
		if (FileExists(recpath.c_str(), "record"))
			RemoveFile(recpath.c_str(), "record");
		if (RenameFile(recpath.c_str(), recordBak, "record") != 0)
			PELOG_ERROR_RETURN((PLV_ERROR, "Compact recovery restore record failed\n"), -1);
	}
	if (FileExists(recpath.c_str(), rnameBak))
	{
		if (FileExists(recpath.c_str(), "rname"))
			RemoveFile(recpath.c_str(), "rname");
		if (RenameFile(recpath.c_str(), rnameBak, "rname") != 0)
			PELOG_ERROR_RETURN((PLV_ERROR, "Compact recovery restore rname failed\n"), -1);
	}
	if (FileExists(recpath.c_str(), recordTmp))
		RemoveFile(recpath.c_str(), recordTmp);
	if (FileExists(recpath.c_str(), rnameTmp))
		RemoveFile(recpath.c_str(), rnameTmp);
	RemoveFile(recpath.c_str(), commit);
#endif
	return 0;
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
	if (cids.empty())
		return 0;
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
	std::lock_guard<std::mutex> lock(_mutex);
	uint32_t rid = 0;
	switch (action.type)
	{
	case Action::BREAK:
		return 0;
	case Action::ADDDIR:
		return addDir(action.name, strlen(action.name), action.isignore, rid, remote);
	case Action::ADDFILE:
	case Action::MODFILE:
		return addFile(action.name, strlen(action.name), action.isignore, action.keephist, rid, remote);
	case Action::DELDIR:
		return delDir(action.name, strlen(action.name), action.isignore, action.keephist, false, remote);
	case Action::DELFILE:
		return delFile(action.name, strlen(action.name), action.isignore, action.keephist, false, remote);
	case Action::RENAME:
		return rename(action.name, action.dst, remote);
	default:
		break;
	}
	PELOG_ERROR_RETURN((PLV_WARNING, "Unsupported action %d\n", action.type), Aresq::NOTIMPLEMENTED);
}