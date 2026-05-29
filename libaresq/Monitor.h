#pragma once

#include <vector>
#include <string>
#include <memory>
#include <thread>
#include <atomic>

#ifdef _WIN32
#	include <windows.h>
#endif

#include "tasking.h"

struct Backup;
class RevisionMgr;
class Monitor;

class WatchJob
{
	friend class Monitor;

	const Backup *backup = nullptr;
	Monitor *parent = nullptr;
	int ibackup = -1;
	std::result_of<decltype(&std::chrono::steady_clock::now)()>::type optm;

#ifdef _WIN32
	HANDLE hDir = INVALID_HANDLE_VALUE;
	DWORD resbuf[16380];
	OVERLAPPED overlap;
#endif

public:
	~WatchJob()
	{
#ifdef _WIN32
		if (hDir != INVALID_HANDLE_VALUE)
			CloseHandle(hDir);
		hDir = INVALID_HANDLE_VALUE;
#endif
	}
};

class Monitor
{
public:
	int run(const std::vector<std::unique_ptr<Backup>> &backups, RevisionMgr &pool);
	void stop();
	void join() { if (watchThrd.joinable()) watchThrd.join(); }
	bool isrunning() const { return watching; }

private:
	RevisionMgr *pool = nullptr;
	std::vector<WatchJob> jobs;
	std::atomic<bool> watching{false};

	std::thread watchThrd;

	int initJobs(const std::vector<std::unique_ptr<Backup>> &backups);
	void watchproc();

#ifdef _WIN32
	HANDLE hStop = NULL;
	static void WINAPI onEvent(DWORD dwErrorCode, DWORD dwNumberOfBytesTransfered, LPOVERLAPPED lpOverlapped);
	int nwatching = 0;
#endif
};
