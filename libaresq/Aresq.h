#pragma once

#include <vector>
#include <memory>
#include <thread>

#include "pe_log.h"
#include "Root.h"
#include "Remote.h"
#include "AresqIgnore.h"
#include "libaresq/fsadapter.h"
#include "libaresq/utfconv.h"
#include "libaresq/RemoteSmb.h"

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
	};

public:
	Aresq();
	~Aresq();

	int init(const std::string &datadir);

	int run();

private:
	std::string recorddir;

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
};

