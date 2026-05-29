#include "stdafx.h"

#include "Aresq.h"
#include "tasking.h"
#include <random>
#include <algorithm>
#include <chrono>
#include <thread>

#include "resguard.h"
#define LIBCONFIG_STATIC
#include "libconfig/libconfig.h"

#ifdef _MSC_VER
#	pragma comment(lib, "Ws2_32.lib")
#endif

Aresq::Aresq()
{
}


Aresq::~Aresq()
{
}

struct configext_t : public config_t	// simple resource manager for config_t
{
	configext_t() { config_init(this); }
	~configext_t() { config_destroy(this); }
};

int Aresq::init(const std::string &datadir)
{
	// load conf file
	configext_t config;
	if (CONFIG_FALSE == config_read_file(&config, (datadir + "/aresq.conf").c_str()))
	{
		PELOG_ERROR_RETURN((PLV_ERROR, "Error loading config file (line %d): %s\n",
			config_error_line(&config), config_error_text(&config)), -1);
	}
	// logs
	pelog_setfile((datadir + "/run.log").c_str(), false);

	// register
	if (regi.init((datadir + "/register").c_str()) != 0)
		PELOG_ERROR_RETURN((PLV_ERROR, "Error opening register\n"), -1);

	// AresqIgnore
	ignore = std::unique_ptr<AresqIgnore>(new AresqIgnore());
	if (ignore->loadglobal((datadir + '/' + "aresqignore").c_str(), true) != 0)
		PELOG_ERROR_RETURN((PLV_ERROR, "Global ignore file %s failed\n", (datadir + '/' + "aresqignore").c_str()), -1);

	// remote
	remote = Remote::fromConfig(&config);
	if (!remote)
		PELOG_ERROR_RETURN((PLV_ERROR, "remote config error\n"), -1);

	// hist
	int keephist = true;
	config_lookup_bool(&config, "general.history", &keephist);

	// backups
	recorddir = datadir + "/records";
	config_setting_t *cbks = config_lookup(&config, "backups");
	if (!cbks || !config_setting_is_group(cbks) || config_setting_length(cbks) == 0)
		PELOG_ERROR_RETURN((PLV_ERROR, "backups config not found\n"), -1);
	for (int i = 0; i < config_setting_length(cbks); ++i)
	{
		config_setting_t *cbk = config_setting_get_elem(cbks, i);
		const char *name = config_setting_name(cbk);
		const char *path = config_setting_get_string(cbk);
		if (!name || !*name || !path || !*path)
			PELOG_ERROR_RETURN((PLV_ERROR, "Invalid backup setting idx(%d)\n", i), -1);

		backups.emplace_back(new Backup);
		backups.back()->id = (int)backups.size() - 1;
		backups.back()->name = name;
		backups.back()->dir = path;
		backups.back()->keephist = keephist != 0;
		if (backups.back()->root.load(backups.back()->id, name, path,
				(recorddir + '/' + name).c_str(), keephist != 0, ignore.get()) != 0)
			PELOG_ERROR_RETURN((PLV_ERROR, "Init ackup idx(%d) %s failed\n", i, name), -1);
	}

	return 0;
}

// Mode 1: Full update
int Aresq::refreshAll()
{
	std::unique_lock<Spinlock> lock(refreshMutex, std::try_to_lock);
	if (!lock.owns_lock())
		PELOG_ERROR_RETURN((PLV_ERROR, "Aresq already running\n"), -1);

	int cret = remote->connect();
	if (cret != OK)
		PELOG_ERROR_RETURN((PLV_ERROR, "refreshAll: connect failed (%d)\n", cret), cret);
	ResGuard<Remote> remote_guard(remote.get(), [](Remote *r) { r->disconnect(); });

	// Load refreshStep
	std::string stepName;
	std::vector<std::string> step;
	{
		std::string stepstr = regi.get("refreshStep");
		const char *p = stepstr.c_str();
		const char *pos = NULL;
		if ((pos = strchr(p, '/')) != NULL && p != pos)
		{
			stepName.assign(p, pos);
			for (p = pos + 1; *p && (pos = strchr(p, '/')) != NULL && pos != p; p = pos + 1)
				step.emplace_back(p, pos);
			if (*p && *p != '/')
				step.emplace_back(p);
		}
		if (!stepName.empty())
			PELOG_LOG((PLV_INFO, "Got saved refresh step %s\n", stepstr.c_str()));
		if (!stepstr.empty() && regi.set("refreshStep", "") != 0)	// clear the refreshStep record
			PELOG_ERROR_RETURN((PLV_ERROR, "clear refreshStep failed\n"), -1);
	}
	// validate stepname
	if (!stepName.empty() && std::none_of(backups.begin(), backups.end(),
			[&](std::unique_ptr<Backup> &backup){ return stepName == backup->name; }))
	{
		PELOG_LOG((PLV_ERROR, "Invalid refresh step name %s\n", stepName.c_str()));
		stepName.clear();
		step.clear();
	}
	if (!stepName.empty())
		PELOG_LOG((PLV_INFO, "Loaded step name %s\n", stepName.c_str()));

	for (std::unique_ptr<Backup> &backup : backups)
	{
		if (!stepName.empty() && stepName != backup->name)
		{
			PELOG_LOG((PLV_DEBUG, "Skip backup item %s\n", backup->name.c_str()));
			continue;
		}
		Root &root = backup->root;
		root.startRefresh(!stepName.empty() && stepName == backup->name ? &step : NULL);
		stepName.clear();
		Root::Action action;
		int state = 0;
		while (true)	// refreshSteps
		{
			if (action.type != Root::Action::BREAK)
				PELOG_LOG((PLV_DEBUG, "refreshStep\n"));
			int res = root.refreshStep(state, action);
			if (res != 1 && res != 0)
				PELOG_ERROR_RETURN((PLV_ERROR, "refreshStep failed %d\n", res), -1);
			if (res == 0)
				break;
			state = root.perform(action, remote.get());

			if (stopFlag)	// if stop() was called, save progress and exit
			{
				PELOG_LOG((PLV_INFO, "Stopping\n"));
				root.refreshSave(&step);
				if (!step.empty())
				{
					std::string stepstr(backup->name);
					for (const std::string &stepdir : step)
						stepstr.append("/").append(stepdir);
					PELOG_LOG((PLV_INFO, "Recording refhresh step %s\n", stepstr.c_str()));
					if (regi.set("refreshStep", stepstr.c_str()) != 0)
						PELOG_LOG((PLV_ERROR, "record refreshStep failed\n"));
				}
				break;
			}
		}	// while (true)	// refreshSteps
		AuAssert(root.verify());
		if (stopFlag)
			break;
	}	// for (std::unique_ptr<Backup> &backup : backups)
	return 0;
}

// Mode 2: Continuous monitoring & incremental update
//
//   Monitor  ->  OS filesystem events -> revisionMgr.submit()
//          |  (revisionMgr.eventQueue)
//   RevisionMgr  ->  organize & 2-min quiesce
//          |  (revisionMgr.items)
//   executor     ->  revisionMgr.popReady() -> executeItem() -> Remote
void Aresq::run()
{
	revisionMgr.start(backups, ignore.get());

	executor = std::thread([this]{ executorProc(); });

	if (monitor.run(backups, revisionMgr) != 0)
	{
		PELOG_LOG((PLV_ERROR, "Monitor::run failed\n"));
		stop();
	}

	monitor.join();
	revisionMgr.notifyStop();  // sends TT_STOP; organizeproc drains then unblocks waitReady
	revisionMgr.join();
	executor.join();
	PELOG_LOG((PLV_INFO, "Aresq::run done\n"));
}

// Thread-safe: signals monitor and executor to stop; run() handles join order.
int Aresq::stop()
{
	PELOG_LOG((PLV_INFO, "To stop\n"));
	stopFlag = true;
	monitor.stop();     // signals watchThrd; organizeThrd exits after draining eventQueue
	return 0;
}

// Executor loop: wait for a ready item, ensure connection, drain the pool,
// then disconnect. On item failure, re-queue it and break (next iteration
// will reconnect before retrying).
void Aresq::executorProc()
{
	while (!stopFlag)
	{
		if (!revisionMgr.waitReady(stopFlag))
			break;

		// Retry connect until success or stop
		while (!remote->isConnected() && !stopFlag)
		{
			int cret = remote->connect();
			if (cret == OK)
				break;
			if (cret == DISCONNECTED)
				PELOG_LOG((PLV_WARNING, "Remote connect failed (network), retry in 2min\n"));
			else
				PELOG_LOG((PLV_WARNING, "Remote connect failed (credential/config err %d), retry in 2min\n", cret));
			stopFlag.wait_for(std::chrono::seconds(120));
		}
		if (stopFlag)
			break;

		// Execute ready items
		while (!stopFlag)
		{
			std::unique_ptr<TaskFile> item = revisionMgr.popReady();
			if (!item)
				break;
			PELOG_LOG((PLV_INFO, "Execute %s: %s\n", item->opname(), item->file1.c_str()));
			int eret = executeItem(*item);
			if (eret == OK)
				continue;
			PELOG_LOG((PLV_ERROR, "Execute failed (%d) %s: %s\n", eret, item->opname(), item->file1.c_str()));
			revisionMgr.putBack(std::move(item));
			break;
		}

		remote->disconnect();
	}
	PELOG_LOG((PLV_INFO, "Aresq::executorProc done\n"));
}

// Dispatch a single ready task to the appropriate Root operation via Remote.
int Aresq::executeItem(TaskFile &item)
{
	Backup &bk = *backups[item.ibackup];
	Root &root = bk.root;
	const char *file1 = item.file1.c_str();
	const char *file2 = item.file2.c_str();

	switch (item.op)
	{
	case TaskFile::TF_NEW:
		if (item.filetype == TaskFile::FT_DIR)
			return root.addDir(file1, remote.get());
		return root.addFile(file1, remote.get());
	case TaskFile::TF_MOD:
		return root.addFile(file1, remote.get());
	case TaskFile::TF_DEL:
		if (item.filetype == TaskFile::FT_DIR)
			return root.delDir(file1, remote.get());
		return root.delFile(file1, remote.get());
	case TaskFile::TF_REN:
		return root.rename(file1, file2, remote.get());
	default:
		PELOG_ERROR_RETURN((PLV_ERROR, "executeItem: unexpected op %s\n", item.opname()), -1);
	}
}

const char *cycode = "faieugrf;owtnpi4u5hutkerfbuoery4ug3";
const char *cypat = "*#**#";
const char *codebook = "6psUoSXW3rVZhI1z";

std::string Aresq::encpwd(const char *code)
{
	if (!code || !*code)
		return "";
	std::string encode = cypat;
	std::mt19937 rng(std::random_device{}());
	std::uniform_int_distribution<uint32_t> dist(0, 0xffffffffu);
	uint32_t seed = dist(rng);
	for (size_t i = 0; i < sizeof(seed); ++i)
	{
		uint8_t c = (seed >> (i * 8)) & 0xff;
		encode.push_back(codebook[c & 0xf]);
		encode.push_back(codebook[(c >> 4) & 0xf]);
	}
	for (uint32_t cp = 0; *code; ++code)
	{
		if (cycode[cp] == 0)
			cp = 0;
		uint8_t c = (uint8_t)*code ^ seed ^ cycode[cp];
		encode.push_back(codebook[c & 0xf]);
		encode.push_back(codebook[(c >> 4) & 0xf]);
		seed = seed * 16777213 + 6423135;
	}
	return encode;
}

inline uint8_t decc(uint8_t c)
{
	for (uint8_t i = 0; codebook[i]; ++i)
	{
		if (codebook[i] == c)
			return i;
	}
	return 16;
}
std::string Aresq::decpwd(const char *code)
{
	if (strncmp(code, cypat, strlen(cypat)) != 0)
		return code;
	code += strlen(cypat);
	uint32_t seed = 0;
	for (size_t i = 0; i < sizeof(seed) * 2; ++i, ++code)
	{
		seed = seed | (decc(*code) << (4 * i));
	}
	std::string decode;
	for (uint32_t cp = 0; code[0] && code[1]; code+= 2)
	{
		if (cycode[cp] == 0)
			cp = 0;
		uint8_t c = decc(code[0]) | (decc(code[1]) << 4);
		decode.push_back(c ^ seed ^ cycode[cp]);
		seed = seed * 16777213 + 6423135;
	}
	return decode;
}
