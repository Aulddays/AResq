#pragma once

#include <vector>
#include <memory>
#include <thread>
#include <atomic>

#include "pe_log.h"
#include "Root.h"
#include "Remote.h"
#include "AresqIgnore.h"
#include "fsadapter.h"
#include "utfconv.h"
#include "RemoteSmb.h"
#include "Register.h"
#include "utils.h"

struct Backup
{
	int id = -1;
	std::string name;
	std::string dir;
	std::string absdir;
	bool keephist = false;

	// dynamic properties
	uint32_t refreshTime = 0;
	uint32_t refreshErrTime = 0;
	uint32_t compactTime = 0;

	Root root;
};

#include "Monitor.h"
#include "RevisionMgr.h"

class Aresq
{
public:
	enum StatusCode
	{
		OK = 0,
		DISCONNECTED = -1,
		CONFLICT = -2,
		REMOTEERR = -3,
		NOTFOUND = -4,
		NOTIMPLEMENTED = -5,
		EPARAM = -6,		// parameter error
		EINTERNAL = -7,	// internal error
		ECANCELE = -8,
		AGAIN = 1,		// operation started; call the step function to continue
		FILELOCKED = -9,		// file exists but failed to read
		INACCESIBLE = -10,	// file or dir inaccessible
	};

public:
	Aresq();
	~Aresq();

	int init(const std::string &datadir);

	// Mode 1: full update
	int refreshAll();
	int refreshDyn();

	// Mode 2: continuous monitoring & incremental update
	//
	//   Monitor  ->  OS filesystem events -> revisionMgr.submit()
	//          |  (revisionMgr.eventQueue)
	//   RevisionMgr  ->  organize & 2-min quiesce
	//          |  (revisionMgr.items)
	//   executor     ->  revisionMgr.popReady() -> executeItem() -> Remote
	int run();          // blocking: starts monitor + executor, returns after stop()
	int stop();          // thread-safe

	int onDataChange(std::unique_ptr<TaskFile> task);

	static std::string encpwd(const char *code);
	static std::string decpwd(const char *code);

private:
	std::string absdatadir;
	std::string recorddir;
	Register regi;

	std::vector<std::unique_ptr<Backup>> backups;

	// remote (executor-thread only after run() starts)
	std::unique_ptr<Remote> remote;

	// ignore
	std::unique_ptr<AresqIgnore> ignore;

	// continuous monitoring
	Monitor monitor;
	RevisionMgr revisionMgr;
	std::thread executor;
	Event stopFlag;
	int idleTimeout;          // general.idle_timeout: executor onIdle() frequency, in seconds
	int fullRefreshInterval;  // general.full_refresh_interval: interval between full refreshes, in seconds
	int monitorCommitDelay;   // general.monitor_commit_delay: time before realtime update got committed, in seconds
	void executorProc();

	// prevents concurrent refreshAll() calls
	Spinlock refreshMutex;

	int refreshOneBackup(Backup &backup);

	int executeItem(TaskFile &item);
	int submitRefreshParent(int ibackup, const char *path, bool force);
	int onIdle();
};
