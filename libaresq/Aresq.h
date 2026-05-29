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
	bool keephist = false;
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
		FILELOCKED = -9,		// file exists but failed to read
		INACCESIBLE = -10,	// file or dir inaccessible
	};

public:
	Aresq();
	~Aresq();

	int init(const std::string &datadir);

	// Mode 1: full update
	int refreshAll();

	// Mode 2: continuous monitoring & incremental update
	//
	//   Monitor  ->  OS filesystem events -> revisionMgr.submit()
	//          |  (revisionMgr.eventQueue)
	//   RevisionMgr  ->  organize & 2-min quiesce
	//          |  (revisionMgr.items)
	//   executor     ->  revisionMgr.popReady() -> executeItem() -> Remote
	void run();          // blocking: starts monitor + executor, returns after stop()
	int stop();          // thread-safe

	static std::string encpwd(const char *code);
	static std::string decpwd(const char *code);

private:
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
	void executorProc();
	Event stopFlag;

	// prevents concurrent refreshAll() calls
	Spinlock refreshMutex;

	int executeItem(TaskFile &item);
};
