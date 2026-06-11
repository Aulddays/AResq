#include "stdafx.h"
#include "Aresq.h"
#include "RevisionMgr.h"
#include "AresqIgnore.h"
#include "fsadapter.h"
#include "pe_log.h"

void RevisionMgr::start(const std::vector<std::unique_ptr<Backup>> &backups_, AresqIgnore *ig, int commitDelay_)
{
	backups = &backups_;
	ignore = ig;
	commitDelay = std::chrono::seconds(commitDelay_);
	items.resize(backups_.size());
	organizeThrd = std::thread([this]{ organizeproc(); });
}
enum PathRel { PR_NONE = -1, PR_SAME = 0, PR_PARENT, PR_ANCESTOR, PR_CHILD, PR_DESCENDANT };

static int pathcmp(const char *p1, const char *p2)
{
	if ((*p1 == 0 || *p2 == 0) && *p1 != *p2)
		return PR_NONE;
	for (;; ++p1, ++p2)
	{
		if (*p1 == 0 && *p2 == 0) return PR_SAME;
		if (*p1 == 0) return *p2 != '/' ? PR_NONE : (strchr(p2 + 1, '/') ? PR_ANCESTOR : PR_PARENT);
		if (*p2 == 0) return *p1 != '/' ? PR_NONE : (strchr(p1 + 1, '/') ? PR_DESCENDANT : PR_CHILD);
		if (*p1 != *p2) return PR_NONE;
	}
}

// ---- helpers (called with mutex held) ---------------------------------------

// Returns true if some item in the pool is ready. (caller must hold mutex)
bool RevisionMgr::hasReadyLocked() const
{
	auto now = std::chrono::steady_clock::now();
	for (const auto &job : items)
		for (const auto &item : job)
			if (now - item->timeSteady >= commitDelay)
				return true;
	return false;
}

// Estimate time until the next item becomes ready based on items in pool.
// Returns duration::max() if the pool is empty. (caller must hold mutex)
std::chrono::steady_clock::duration RevisionMgr::timeUntilReadyLocked() const
{
	auto earliest = std::chrono::steady_clock::time_point::max();
	for (const auto &job : items)
		for (const auto &item : job)
			if (item->timeSteady < earliest)
				earliest = item->timeSteady;

	if (earliest == std::chrono::steady_clock::time_point::max())
		return std::chrono::steady_clock::duration::max();  // pool empty

	auto readyAt = earliest + commitDelay;
	auto now = std::chrono::steady_clock::now();
	if (readyAt <= now)
		return std::chrono::steady_clock::duration::zero();
	return readyAt - now;
}

// ---- start / organizeproc / join --------------------------------------------

void RevisionMgr::organizeproc()
{
	bool stop = false;
	while (!stop)
	{
		std::unique_ptr<Task> task = eventQueue.get();
		switch (task->tasktype)
		{
		case Task::TT_STOP:
			stop = true;
			break;
		case Task::TT_FILE:
		{
			std::unique_ptr<TaskFile> tf(static_cast<TaskFile *>(task.release()));
			process(std::move(tf));
			break;
		}
		default:
			break;
		}
	}
	cv.notify_all();  // unblock waitReady after stop
	stopped = true;
}

void RevisionMgr::join()
{
	if (organizeThrd.joinable())
		organizeThrd.join();
}

// ---- submit (thread-safe: enqueue for organizeproc) -------------------------

int RevisionMgr::submit(std::unique_ptr<TaskFile> task)
{
	if (stopped)
		return -1;
	eventQueue.put(std::move(task));
	return 0;
}

// ---- process (called from organizeproc: merge/dedup into items) -------------

void RevisionMgr::process(std::unique_ptr<TaskFile> task)
{
	std::lock_guard<std::mutex> lock(mutex);

	int ibackup = task->ibackup;
	const Backup &backup = *(*backups)[ibackup];
	if (ibackup + 1 == backups->size())	// datadir change
	{
		aresq->onDataChange(std::move(task));
		return;
	}
	// special: ignore datadir changes if datadir is inside one of the normal backup dirs
	{
		std::string filepath = backup.dir + '/' + task->file1;
		const std::string &datapath = backups->back()->absdir;	// backups.back() is datadir
		if (filepath.length() >= datapath.length() &&
				strncmp(filepath.c_str(), datapath.c_str(), datapath.length()) == 0 &&
				(filepath.length() == datapath.length() || filepath[datapath.length()] == '/'))
			PELOG_ERROR_RETURNVOID((PLV_VERBOSE, "Ignore datadir change %s\n", task->file1.c_str()));
	}

	auto &ops = items[ibackup];

	// make sure REN_DST places right after REN_SRC, and REN_SRC can only be in front of REN_DST
	if (task->op == TaskFile::TF_REN_DST)
	{
		// validate the corresponding src
		if (ops.empty() || ops.back()->op != TaskFile::TF_REN_SRC)
		{
			PELOG_LOG((PLV_ERROR, "Drop rename dst with no src %s\n", task->file1.c_str()));
			goto NOTIFY;
		}
	}
	else if (!ops.empty() && ops.back()->op == TaskFile::TF_REN_SRC)
	{
		PELOG_LOG((PLV_ERROR, "Drop rename src with no dst %s\n", ops.back()->file1.c_str()));
		ops.pop_back();
	}

	// ignore filtering
	if (task->op == TaskFile::TF_REN_DST)	// rename case
	{
		bool igs = ignore && ignore->isignore_p(ops.back()->file1.c_str(), ops.back()->filetype == TaskFile::FT_DIR);
		bool igd = ignore && ignore->isignore_p(task->file1.c_str(), task->filetype == TaskFile::FT_DIR);
		if (igs && igd)
		{
			ops.pop_back();
			goto NOTIFY_DUMP;
		}
		else if (igs)
		{
			ops.pop_back();
			task->op = TaskFile::TF_NEW;
			PELOG_LOG((PLV_VERBOSE, "REN from ignore -> NEW %s\n", task->file1.c_str()));
		}
		else if (igd)
		{
			task.swap(ops.back());
			ops.pop_back();
			task->op = TaskFile::TF_DEL;
			PELOG_LOG((PLV_VERBOSE, "REN to ignore -> DEL %s\n", task->file1.c_str()));
		}
	}
	else if (task->op != TaskFile::TF_REN_SRC && task->op != TaskFile::TF_REFRESH &&
		ignore && ignore->isignore_p(task->file1.c_str(), task->filetype == TaskFile::FT_DIR))	// non-ren case, just ignore
	{
		goto NOTIFY;
	}

	// resolve file type
	if (task->op == TaskFile::TF_REN_DST || task->op == TaskFile::TF_NEW || task->op == TaskFile::TF_MOD)
	{
		uint64_t ftime, fsize;
		bool isdir;
		if (getFileAttr(backup.dir.c_str(), task->file1.c_str(), task->file1.length(), ftime, fsize, isdir) == 0)
			task->filetype = isdir ? TaskFile::FT_DIR : TaskFile::FT_FILE;
	}

	if (task->op != TaskFile::TF_REN_SRC || !ignore || !ignore->isignore_p(task->file1.c_str(), false))
	{
		PELOG_LOG((PLV_INFO, "FileOp %s (%s): %s\n", task->opname(), backup.name.c_str(), task->file1.c_str()));
	}

	{
		int nidx = -1;
		if (task->op == TaskFile::TF_MOD && task->filetype == TaskFile::FT_DIR)
			goto NOTIFY;  // ignore dir MOD

		ops.push_back(std::move(task));

		if (ops.back()->op == TaskFile::TF_REN_SRC)
			goto NOTIFY_DUMP;  // wait for matching REN_DST

		nidx = (int)ops.size() - 1;
		if (ops.back()->op == TaskFile::TF_REN_DST)
		{
			AuAssert(nidx > 0 && ops[nidx - 1]->op == TaskFile::TF_REN_SRC);
			if (ops[nidx]->file1 == ops[nidx - 1]->file1)
			{
				PELOG_LOG((PLV_VERBOSE, "MERGEOP drop REN with same src/dst %s\n", ops[nidx]->file1.c_str()));
				ops.erase(ops.begin() + nidx - 1, ops.end());
				goto NOTIFY_DUMP;
			}
			--nidx;  // now nidx must be TF_REN_SRC
		}

		for (int oidx = nidx - 1; oidx >= 0; --oidx)	// check for previous items for merge
		{
			TaskFile &nop = *ops[nidx];
			TaskFile &oop = *ops[oidx];

			int rel = pathcmp(nop.file1.c_str(), oop.file1.c_str());

			// update type
			if (nop.filetype == TaskFile::FT_UNK && rel == PR_SAME && oop.filetype != TaskFile::FT_UNK)
				nop.filetype = oop.filetype;
			if (nop.filetype == TaskFile::FT_UNK && (rel == PR_PARENT || rel == PR_ANCESTOR))
				nop.filetype = TaskFile::FT_DIR;

			// ignore dir MOD
			if (nop.op == TaskFile::TF_MOD && nop.filetype == TaskFile::FT_DIR)
			{
				ops.erase(ops.begin() + nidx);
				goto NOTIFY;
			}

			if (nop.op == TaskFile::TF_REFRESH && oop.op == TaskFile::TF_REFRESH && rel == PR_SAME)
			{
				PELOG_LOG((PLV_VERBOSE, "MERGEOP REFRESH:%s keep old, force %d -> %d\n",
					oop.file1.c_str(), oop.force, oop.force || nop.force));
				oop.force = oop.force || nop.force;
				ops.erase(ops.begin() + nidx);
				break;
			}

			// detect cross-dir move, which usually manifests as DEL + NEW (and no sub-NEW for the dir case)
			if (nop.op == TaskFile::TF_NEW && oop.op == TaskFile::TF_DEL && oidx == nidx - 1 &&
				(nop.timeSteady - oop.timeSteady) / std::chrono::seconds(1) <= 1)	// allow 1s gap between DEL and NEW
			{
				// check name & type
				const char *nname = baseName(nop.file1.c_str(), nop.file1.length());
				const char *oname = baseName(oop.file1.c_str(), oop.file1.length());
				int oitype = (*backups)[ibackup]->root.getRecordType(oop.file1.c_str());
				TaskFile::FileType otype = oitype == 1 ? TaskFile::FT_DIR : (oitype == 0 ? TaskFile::FT_FILE : TaskFile::FT_UNK);
				if (otype == nop.filetype && otype != TaskFile::FT_UNK && strcmp(nname, oname) == 0)
				{
					PELOG_LOG((PLV_VERBOSE, "MERGEOP cross-dir move detected: DEL + NEW -> REN %s -> %s\n", oop.file1.c_str(), nop.file1.c_str()));
					oop.op = TaskFile::TF_REN_SRC;
					nop.op = TaskFile::TF_REN_DST;
					nop.recur = nop.filetype == TaskFile::FT_DIR;
					if (ops[nidx]->file1 == ops[nidx - 1]->file1)
					{
						PELOG_LOG((PLV_VERBOSE, "MERGEOP drop REN with same src/dst %s\n", ops[nidx]->file1.c_str()));
						ops.erase(ops.begin() + nidx - 1, ops.begin() + nidx + 1);
						goto NOTIFY_DUMP;
					}
					--nidx;	// since REN was created, update nidx to follow the convention (see before the `for`)
					continue;	// continue with merge (but move on to next oop after --nidx), especially the REN specific rules
				}
			}

		
			if (nop.op == TaskFile::TF_NEW)
			{
				nop.recur = nop.filetype == TaskFile::FT_DIR;
				break;	// no merge for NEW other than cross-dir move
			}

			// skip if no match
			if (rel == PR_NONE)
				continue;
			// stop merge if previously deleted
			if (oop.op == TaskFile::TF_DEL && (rel == PR_SAME || rel == PR_CHILD || rel == PR_DESCENDANT))
				break;

			// previous new or mod merges into new mod
			if (nop.op == TaskFile::TF_MOD && rel == PR_SAME && (oop.op == TaskFile::TF_MOD || oop.op == TaskFile::TF_NEW))
			{
				PELOG_LOG((PLV_VERBOSE, "MERGEOP %s + %s -> %s: %s\n", oop.opname(), nop.opname(), oop.opname(), nop.file1.c_str()));
				nop.op = oop.op;
				ops.erase(ops.begin() + oidx);
				break;
			}

			// del covers previous ops
			if (nop.op == TaskFile::TF_DEL)
			{
				if (oop.op == TaskFile::TF_NEW && rel == PR_SAME)
				{
					PELOG_LOG((PLV_VERBOSE, "MERGEOP NEW + DEL offset: %s\n", nop.file1.c_str()));
					ops.erase(ops.begin() + nidx);
					ops.erase(ops.begin() + oidx);
					break;
				}
				if ((oop.op == TaskFile::TF_NEW || oop.op == TaskFile::TF_MOD || oop.op == TaskFile::TF_DEL) &&
					(rel == PR_SAME || rel == PR_PARENT || rel == PR_ANCESTOR))
				{
					PELOG_LOG((PLV_VERBOSE, "MERGEOP DEL:%s disqualifies previous %s:%s\n", nop.file1.c_str(), oop.opname(), oop.file1.c_str()));
					ops.erase(ops.begin() + oidx);
					--nidx;
					continue;
				}
				if (oop.op == TaskFile::TF_REN_DST && rel == PR_SAME)
				{
					PELOG_LOG((PLV_VERBOSE, "MERGEOP REN + DEL -> DEL %s\n", ops[oidx - 1]->file1.c_str()));
					// if DEL is the newest and is right after REN, remove REN (and keep DEL's time), so that DEL and future cross-dir NEW would merge
					// otherwise move DEL to REN (and keep REN's time & order), so that NEW between DEL and REN would preserve
					ops[oidx - 1]->op = TaskFile::TF_DEL;
					if (nidx == ops.size() - 1 && oidx == ops.size() - 2)
						ops[oidx - 1]->timeSteady = ops[nidx]->timeSteady;
					ops.erase(ops.begin() + nidx);
					ops.erase(ops.begin() + oidx);
					break;
				}
			}

			// REN
			if (nop.op == TaskFile::TF_REN_SRC)
			{
				AuAssert(nidx + 1 < (int)ops.size() && ops[nidx + 1]->op == TaskFile::TF_REN_DST);
				if (oop.op == TaskFile::TF_NEW && rel == PR_SAME)
				{
					PELOG_LOG((PLV_VERBOSE, "MERGEOP NEW:%s + REN -> NEW:%s\n", oop.file1.c_str(), ops[nidx + 1]->file1.c_str()));
					ops[nidx + 1]->op = TaskFile::TF_NEW;
					ops.erase(ops.begin() + nidx);
					ops.erase(ops.begin() + oidx);
					break;
				}
				if (oop.op == TaskFile::TF_MOD && rel == PR_SAME)
				{
					PELOG_LOG((PLV_VERBOSE, "MERGEOP MOD + REN -> REN + MOD %s : %s\n", oop.file1.c_str(), ops[nidx + 1]->file1.c_str()));
					// manage seq
					uint64_t savedSeq = ops[nidx + 1]->seq;
					for (int i = nidx + 1; i > oidx; --i)
						ops[i]->seq = ops[i - 1]->seq;
					ops.erase(ops.begin() + oidx);
					// nidx was REN_SRC, then nidx must be REN_DST after erase. insert MOD after current nidx
					// nop and oop got invalidated now, must use idx instead of nop/oop
					auto newmod = std::make_unique<TaskFile>(ops[0]->ibackup, TaskFile::TF_MOD, ops[nidx]->file1.c_str());
					newmod->seq = savedSeq;
					newmod->time = ops[nidx]->time;
					newmod->timeSteady = ops[nidx]->timeSteady;
					ops.emplace(ops.begin() + nidx + 1, std::move(newmod));
					break;
				}
				if ((oop.op == TaskFile::TF_NEW || oop.op == TaskFile::TF_MOD) && (rel == PR_PARENT || rel == PR_ANCESTOR))
				{
					PELOG_LOG((PLV_VERBOSE, "MERGEOP REN complicated %s : %s\n", oop.file1.c_str(), ops[nidx + 1]->file1.c_str()));
					oop.recur = true;
					ops[nidx + 1]->recur = true;
					break;
				}
			}
		}

		// After the normal merge, detect DEL+NEW(file) and merge into MOD
		// Do this separately because a MOD of `foo` is actually often a complicated process:
		// NEW foo.tmp -> WRITE foo.tmp -> REN foo foo.tmp1 -> REN foo.tmp foo -> DEL foo.tmp1
		// which is difficult to detect in the first round of merge
		for (size_t i = 0; i + 1 < ops.size(); ++i)
		{
			TaskFile &delop = *ops[i];
			TaskFile &newop = *ops[i + 1];
			if (delop.op != TaskFile::TF_DEL || newop.op != TaskFile::TF_NEW)
				continue;
			if (newop.filetype != TaskFile::FT_FILE)
				continue;
			if (pathCmpMt(delop.file1.c_str(), newop.file1.c_str()) != 0)
				continue;
			if ((*backups)[ibackup]->root.getRecordType(newop.file1.c_str()) != 0)
				continue;

			PELOG_LOG((PLV_VERBOSE, "MERGEOP DEL + NEW -> MOD: %s\n", newop.file1.c_str()));
			newop.op = TaskFile::TF_MOD;
			ops.erase(ops.begin() + i);
		}
	}

NOTIFY_DUMP:
	PELOG_LOG((PLV_DEBUG, "Dump Pool [%d:%d] (%s):\n", ibackup, (int)ops.size(), backup.name.c_str()));
	for (const auto &op : ops)
		PELOG_LOG((PLV_DEBUG, "  %s: %s\n", op->opname(), op->file1.c_str()));
NOTIFY:
	cv.notify_all();
}

// ---- waitReady --------------------------------------------------------------

// Block until an item has aged commitDelay and is ready to pop.
// Returns true when ready, false if stopFlag is set or maxWait expires.
bool RevisionMgr::waitReady(const Event &stopFlag, std::chrono::steady_clock::duration maxWait)
{
	using Clock = std::chrono::steady_clock;
	std::unique_lock<std::mutex> lk(mutex);
	const auto deadline = maxWait == Clock::duration::max() ? Clock::time_point::max() : Clock::now() + maxWait;
	while (!stopFlag)
	{
		if (hasReadyLocked())	// if already ready, just return
			return true;

		auto now = Clock::now();
		if (now >= deadline)
			break;

		auto dt = timeUntilReadyLocked();	// Estimate the time to become ready, based on items in pool
		auto wakeAt = dt == Clock::duration::max() ? Clock::time_point::max() : now + dt;
		if (deadline < wakeAt)
			wakeAt = deadline;
		cv.wait_until(lk, wakeAt, [&]{
			if (stopFlag)
				return true;
			if (dt == Clock::duration::max())	// if pool was empty and cv got notified
			{
				for (const auto &j : items)	// stop wait if new item got added to the pool
					if (!j.empty())
						return true;
			}
			return false;
		});
		// waited, check again in next loop
	}
	return false;	// stopped or timed out
}

// ---- popReady / pushFront / notifyStop --------------------------------------

std::unique_ptr<TaskFile> RevisionMgr::popReady()
{
	std::lock_guard<std::mutex> lk(mutex);
	auto now = std::chrono::steady_clock::now();

	for (;;)
	{
		int bestBkQueue = -1, bestIdx = -1;
		int64_t bestSeq = INT64_MAX;

		for (int j = 0; j < (int)items.size(); ++j)
			for (int i = 0; i < (int)items[j].size(); ++i)
			{
				const auto &item = items[j][i];
				if (now - item->timeSteady >= commitDelay && item->seq < bestSeq)
				{
					bestSeq = item->seq;
					bestBkQueue = j;
					bestIdx = i;
				}
			}

		if (bestBkQueue < 0)
			return nullptr;

		std::unique_ptr<TaskFile> result = std::move(items[bestBkQueue][bestIdx]);
		items[bestBkQueue].erase(items[bestBkQueue].begin() + bestIdx);

		// Merge REN_SRC + following REN_DST into a single TF_REN
		// REN_DST should go right after REN_SRC, so items[bestBkQueue][bestIdx] should be REN_DST now
		if (result->op == TaskFile::TF_REN_SRC)
		{
			auto &bkQueue = items[bestBkQueue];
			if (bestIdx < (int)bkQueue.size() && bkQueue[bestIdx]->op == TaskFile::TF_REN_DST)
			{
				result->op    = TaskFile::TF_REN;
				result->file2 = bkQueue[bestIdx]->file1;
				result->recur = result->recur || bkQueue[bestIdx]->recur;
				bkQueue.erase(bkQueue.begin() + bestIdx);
			}
			else
			{
				// REN_DST missing: discard orphaned REN_SRC and try again
				PELOG_LOG((PLV_ERROR, "REN_SRC without REN_DST, dropping: %s\n", result->file1.c_str()));
				continue;
			}
		}

		return result;
	}
}

void RevisionMgr::putBack(std::unique_ptr<TaskFile> task)
{
	std::lock_guard<std::mutex> lk(mutex);
	int j = task->ibackup;

	// Split TF_REN back into REN_SRC + REN_DST
	if (task->op == TaskFile::TF_REN)
	{
		auto dst = std::make_unique<TaskFile>(j, TaskFile::TF_REN_DST, task->file2.c_str());
		dst->seq = task->seq + 1;
		dst->time = task->time;
		dst->timeSteady = task->timeSteady;
		dst->filetype = task->filetype;
		dst->recur = task->recur;
		dst->force = task->force;
		dst->failnum = task->failnum;
		task->op = TaskFile::TF_REN_SRC;
		task->file2.clear();
		items[j].insert(items[j].begin(), std::move(dst));
	}

	items[j].insert(items[j].begin(), std::move(task));
}

void RevisionMgr::notifyStop()
{
	eventQueue.stop();  // sends TT_STOP; organizeproc drains then calls cv.notify_all()
}
