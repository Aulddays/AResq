#include "stdafx.h"
#include "Aresq.h"
#include "Monitor.h"
#include "RevisionMgr.h"
#include "pe_log.h"
#include <map>

// ---- run (platform-independent) ---------------------------------------------

int Monitor::run(const std::vector<std::unique_ptr<Backup>> &backups, RevisionMgr &p)
{
	if (watching)
		return -1;
	pool = &p;
	jobs.clear();
	if (backups.empty())
		return 0;
	jobs.reserve(backups.size());
	if (initJobs(backups) != 0)
		return -1;
	watching = true;
	watchThrd = std::thread(&Monitor::watchproc, this);
	return 0;
}

// ---- Win32: initJobs / watchproc / onEvent / stop ---------------------------

#ifdef _WIN32

int Monitor::initJobs(const std::vector<std::unique_ptr<Backup>> &backups)
{
	AuAssert(jobs.empty());
	std::vector<wchar_t> buf(MAX_PATH);
	for (size_t i = 0; i < backups.size(); ++i)
	{
		const std::string &dir = backups[i]->dir;
		if (dir.empty())
			PELOG_ERROR_RETURN((PLV_ERROR, "initJobs(%zu) no dir\n", i), -1);
		if (dir.length() + 1 > buf.size())
			buf.resize(dir.length() + 1);
		if (MultiByteToWideChar(CP_ACP, 0, dir.c_str(), -1, buf.data(), (int)buf.size()) == 0)
			PELOG_ERROR_RETURN((PLV_ERROR, "initJobs(%zu) invalid dir: %s\n", i, dir.c_str()), -1);
		jobs.resize(jobs.size() + 1);
		jobs.back().backup = backups[i].get();
		jobs.back().parent = this;
		jobs.back().ibackup = (int)i;
		jobs.back().hDir = CreateFileW(buf.data(), FILE_LIST_DIRECTORY,
			FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL,
			OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED, NULL);
		if (jobs.back().hDir == INVALID_HANDLE_VALUE)
			PELOG_ERROR_RETURN((PLV_ERROR, "initJobs(%zu) CreateFileW failed: %s\n", i, dir.c_str()), -1);
	}
	if (jobs.empty())
		PELOG_ERROR_RETURN((PLV_ERROR, "initJobs: no jobs\n"), -1);
	static_assert(sizeof(jobs[0].overlap.hEvent) >= sizeof(WatchJob *), "hEvent cannot hold a pointer");
	for (size_t i = 0; i < jobs.size(); ++i)
		jobs[i].overlap.hEvent = (HANDLE)&jobs[i];
	if (hStop != NULL)
		CloseHandle(hStop);
	if ((hStop = CreateEvent(NULL, FALSE, FALSE, NULL)) == NULL)
		PELOG_ERROR_RETURN((PLV_ERROR, "initJobs: CreateEvent failed\n"), -1);
	return 0;
}

void Monitor::watchproc()
{
	for (WatchJob &job : jobs)
	{
		++nwatching;
		// async ReadDirectoryChangesW() with a onEvent() callback.
		// The "waiting" will be started by entering "alertable state" later:
		// SleepEx(..., TRUE) if no other events, or WaitForXXObjectEx(..., TRUE) with other events
		if (!ReadDirectoryChangesW(
			job.hDir, job.resbuf, sizeof(job.resbuf), TRUE,
			FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_DIR_NAME |
			FILE_NOTIFY_CHANGE_LAST_WRITE | FILE_NOTIFY_CHANGE_SIZE,
			NULL, &job.overlap, onEvent))
		{
			PELOG_LOG((PLV_ERROR, "Start watching failed %s\n", job.backup->dir.c_str()));
			--nwatching;
		}
	}
	if (nwatching == 0)
	{
		watching = false;
		PELOG_LOG((PLV_ERROR, "Monitor::watchproc nothing to watch\n"));
		return;
	}
	while (nwatching > 0)
	{
		// WaitForSingleObjectEx waits hStop AND enters "alertable state", which also waits ReadDirectoryChangesW()
		DWORD dwres = WaitForSingleObjectEx(hStop, INFINITE, TRUE);
		// ==WAIT_IO_COMPLETION: onEvent() of ReadDirectoryChangesW() was called
		if (dwres != WAIT_OBJECT_0 && dwres != WAIT_FAILED && dwres != WAIT_IO_COMPLETION)
			PELOG_LOG((PLV_WARNING, "WaitForSingleObjectEx ret %lu\n", dwres));
		if (dwres == WAIT_OBJECT_0 || dwres == WAIT_FAILED)
			break;
	}
	if (nwatching > 0)
		for (WatchJob &job : jobs)
			CancelIo(job.hDir);	// CancelIo() is async.
	while (nwatching > 0)
		SleepEx(INFINITE, TRUE);	// Wait for CancelIo() to complete by entering "alertable state"
	watching = false;
	PELOG_LOG((PLV_INFO, "Monitor::watchproc done\n"));
}

void WINAPI Monitor::onEvent(DWORD err, DWORD /*dwlen*/, LPOVERLAPPED lpOverlapped)
{
	if (err != 0)
		PELOG_LOG((PLV_WARNING, "Monitor::onEvent err %lu\n", err));
	WatchJob &job = *(WatchJob *)lpOverlapped->hEvent;
	Monitor *pthis = job.parent;

	if (err == ERROR_OPERATION_ABORTED)
	{
		--pthis->nwatching;
		PELOG_LOG((PLV_INFO, "Monitor: watching stopped, remaining %d\n", pthis->nwatching));
		return;
	}

	static const std::map<DWORD, TaskFile::FileOP> opmap = {
		{ FILE_ACTION_ADDED,            TaskFile::TF_NEW     },
		{ FILE_ACTION_REMOVED,          TaskFile::TF_DEL     },
		{ FILE_ACTION_MODIFIED,         TaskFile::TF_MOD     },
		{ FILE_ACTION_RENAMED_OLD_NAME, TaskFile::TF_REN_SRC },
		{ FILE_ACTION_RENAMED_NEW_NAME, TaskFile::TF_REN_DST },
	};

	std::vector<char> namebuf(MAX_PATH * 4);
	for (FILE_NOTIFY_INFORMATION *event = (FILE_NOTIFY_INFORMATION *)job.resbuf;
		event;
		event = event->NextEntryOffset ? (FILE_NOTIFY_INFORMATION *)((uint8_t *)event + event->NextEntryOffset) : NULL)
	{
		DWORD namelen = event->FileNameLength / sizeof(wchar_t);
		if (namelen * 4 + 1 >= namebuf.size())
			namebuf.resize(namelen * 4 + 1);
		int clen = WideCharToMultiByte(CP_UTF8, 0, event->FileName, (int)namelen,
			namebuf.data(), (int)namebuf.size(), NULL, NULL);
		if (clen <= 0) { PELOG_LOG((PLV_ERROR, "WideCharToMultiByte failed\n")); continue; }
		namebuf[clen] = 0;
		for (char *p = namebuf.data(); *p; ++p)
			if (*p == '\\') *p = '/';

		auto iop = opmap.find(event->Action);
		if (iop == opmap.end()) { PELOG_LOG((PLV_WARNING, "Unknown action %lu\n", event->Action)); continue; }
		if (pthis->pool->submit(std::make_unique<TaskFile>(job.ibackup, iop->second, namebuf.data())) != 0)
		{
			PELOG_LOG((PLV_ERROR, "RevisionMgr stopped, stopping monitor\n"));
			pthis->stop();
			return;
		}
	}

	if (!ReadDirectoryChangesW(
		job.hDir, job.resbuf, sizeof(job.resbuf), TRUE,
		FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_DIR_NAME |
		FILE_NOTIFY_CHANGE_LAST_WRITE | FILE_NOTIFY_CHANGE_SIZE,
		NULL, &job.overlap, onEvent))
	{
		PELOG_LOG((PLV_ERROR, "Re-watch failed (%lu) %s\n", GetLastError(), job.backup->dir.c_str()));
		--pthis->nwatching;
	}
}

void Monitor::stop()
{
	if (!watching)
		return;
	SetEvent(hStop);
}

#else  // !_WIN32

// ---- non-Win32: initJobs / watchproc / stop ---------------------------------

int Monitor::initJobs(const std::vector<std::unique_ptr<Backup>> &backups)
{
	for (size_t i = 0; i < backups.size(); ++i)
	{
		jobs.resize(jobs.size() + 1);
		jobs.back().backup = backups[i].get();
		jobs.back().parent = this;
		jobs.back().ibackup = (int)i;
	}
	return 0;
}

void Monitor::watchproc()
{
	while (watching.load())
		std::this_thread::sleep_for(std::chrono::milliseconds(50));
	PELOG_LOG((PLV_INFO, "Monitor::watchproc stub done\n"));
}

void Monitor::stop()
{
	watching = false;
}

#endif  // !_WIN32
