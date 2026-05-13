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

	int refreshAll();
	int stop() { PELOG_LOG((PLV_INFO, "To stop\n")); running.clear(); return 0; };

	static std::string encpwd(const char *code);
	static std::string decpwd(const char *code);

private:
	std::string recorddir;
	Register regi;

	struct Backup
	{
		int id = -1;
		std::string name;
		std::string dir;
		Root root;
	};
	std::vector<std::unique_ptr<Backup>> backups;

	// worker
	std::thread worker;
	std::unique_ptr<Remote> remote;

	// ignore
	std::unique_ptr<AresqIgnore> ignore;

	// a flag to notify the worker to stop
	AtomicFlag running;
};

