#include "stdafx.h"
#include "Root.h"
#include <stack>
#include "fsadapter.h"
#include "utfconv.h"
#include "pe_log.h"


Root::Root(const char *recpath, const char *root):
	recpath(recpath), root(root)
{

}

Root::~Root()
{
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

int Root::startRefresh()
{
	restate.clear();
	restate.resize(1);
	restate.back().rid = 1;
	reid = 1;
	return 0;
}

int Root::refreshStep(int state)
{
	// check restate first
	if (restate.size() == 0)
		return 1;
	{
		if (restate[0].rid != 1)
		{
			restate.clear();
			PELOG_ERROR_RETURN((PLV_ERROR, "Invalid refresh root\n"), -1);
		}
		for (size_t i = 1; i < restate.size(); ++i)
		{
			RefreshIter &reiter = restate[i];
			RecordItem &rec = _records[reiter.rid];
			if (rec.isdel() || !rec.isdir() || rec.parent() == restate[i - 1].rid ||
				reiter.stage != RefreshIter::INIT && pathCmpMt(reiter.path, rec.name(_rname)) != 0)
			{
				PELOG_LOG((PLV_ERROR, "Invalid refresh state %d. move back\n", (int)i));
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
			BuildPath(pathparts.data(), pathparts.size(), reiter.path);
			// get dir contents
			if (ListDir(reiter.path, reiter.dirs, reiter.files) != 0)
			{
				// list dir failed. maybe it has just been deleted
				if (restate.size() <= 1)
					PELOG_LOG((PLV_ERROR, "Root dir listing failed: %s\n", root.c_str()));
				else
					reiter.stage = RefreshIter::REDOUPPER;	// just rerun parent dir
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
					assert(reiter.prog != 0);
					if (_records[reiter.prog].isdir())
					{
						reiter.stage = RefreshIter::DELDIR;
						reiter.prog = 0;
					}
					return 0;
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
					assert(reiter.prog == 0 || *_records[reiter.prog].name(_rname));
					if (reiter.prog == 0)	// no more rec dirs, move on
					{
						reiter.stage = RefreshIter::FILE;
						reiter.prog = 0;
					}
					return 0;
				}
			}
			// no more DELFILE if reach here, move on to next stage
			reiter.stage = RefreshIter::DELDIR;
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
				if (fid != 0 && (_records[fid].isdel() ||
					pathCmpMt(_records[fid].name(_rname), reiter.files[reiter.prog].name) != 0))
				{
					PELOG_LOG((PLV_DEBUG, "ADD file detected %s: %s\n", reiter.path.buf(), reiter.files[reiter.prog].name));
					reiter.prog++;	// move forward before return
					return 0;
				}
			}
			break;
		}

		case RefreshIter::DIR:
		case RefreshIter::RECUR:

		case RefreshIter::REDOUPPER:		// go back to parent dir and run again
			if (restate.size() <= 1)
				reiter.stage = RefreshIter::RETURN;
			else
			{
				restate.pop_back();
				restate.back().stage = RefreshIter::INIT;
				restate.back().files.clear();
				restate.back().dirs.clear();
			}
			break;

		case RefreshIter::RETURN:	// finished current dir, go back to parent
			if (restate.size() <= 1)
			{
				restate.clear();
				return 1;
			}
			restate.pop_back();
			assert(restate.back().stage == RefreshIter::RECUR);
			break;
		default:
			assert(false);
			break;
		}
	}
	return 0;
}

//int refreshBuildPath(abuf<char> &path)
//{
//
//}
