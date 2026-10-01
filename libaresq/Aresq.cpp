#include "stdafx.h"

#include "Aresq.h"
#include "tasking.h"
#include <random>
#include <algorithm>
#include <chrono>
#include <thread>
#include <set>

#include "resguard.h"
#define LIBCONFIG_STATIC
#include "libconfig/libconfig.h"

#ifdef _MSC_VER
#	pragma comment(lib, "Ws2_32.lib")
#endif

Aresq::Aresq(): revisionMgr(this)
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
	absdatadir = realpath(datadir.c_str());

	// load conf file
	configext_t config;
	if (CONFIG_FALSE == config_read_file(&config, (datadir + "/aresq.conf").c_str()))
	{
		PELOG_ERROR_RETURN((PLV_ERROR, "Error loading config file (%s : %d): %s\n",
			(datadir + "/aresq.conf").c_str(), config_error_line(&config), config_error_text(&config)), -1);
	}
	// logs
	int logToFile = false;
	config_lookup_bool(&config, "general.log_to_file", &logToFile);
	if (logToFile)
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
	// file size limit
	int64_t max_file = 200;
	config_lookup_int64(&config, "general.max_file_mb", &max_file);
	max_file *= 1024 * 1024;

	// monitor configs
	idleTimeout = 300;
	config_lookup_int(&config, "general.idle_timeout", &idleTimeout);
	idleTimeout = std::max(30, std::min(600, idleTimeout));
	fullRefreshInterval = 432000;
	config_lookup_int(&config, "general.full_refresh_interval", &fullRefreshInterval);
	fullRefreshInterval = std::max(86400, std::min(86400 * 10, fullRefreshInterval));
	monitorCommitDelay = 120;
	config_lookup_int(&config, "general.monitor_commit_delay", &monitorCommitDelay);
	monitorCommitDelay = std::max(10, std::min(idleTimeout * 2 / 3, monitorCommitDelay));

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
		backups.back()->absdir = realpath(path);
		backups.back()->keephist = keephist != 0;
		if (backups.back()->root.load(backups.back()->id, name, path,
				(recorddir + '/' + name).c_str(), keephist != 0, ignore.get(), max_file) != 0)
			PELOG_ERROR_RETURN((PLV_ERROR, "Init ackup idx(%d) %s failed\n", i, name), -1);
		backups.back()->refreshTime = regi.geti(name, "refreshTime", 0);
		backups.back()->refreshErrTime = regi.geti(name, "refreshErrTime", 0);
		backups.back()->compactTime = regi.geti(name, "compactTime", 0);
	}

	// `datadir`: special backup
	backups.emplace_back(new Backup);
	backups.back()->id = (int)backups.size() - 1;
	backups.back()->dir = datadir;
	backups.back()->absdir = absdatadir;

	return 0;
}

// Mode 1: Full update
int Aresq::refreshAll()
{
	int res = 0;
	for (std::unique_ptr<Backup> &backup : backups)
	{
		if (!backup->root.loaded())
			continue;
		if ((res = refreshOneBackup(*backup)) != OK)
			break;
		if (stopFlag)
			break;
	}	// for (std::unique_ptr<Backup> &backup : backups)
	return 0;
}

int Aresq::refreshDyn()
{
	int res = 0;
	uint32_t now = (uint32_t)time64(NULL);
	int pick = -1;
	for (int i = 0; i < (int)backups.size(); ++i)	// look for the oldest backup
	{
		if (backups[i]->root.loaded() &&
			(backups[i]->refreshTime + fullRefreshInterval < now || backups[i]->refreshTime > now + fullRefreshInterval) &&
			(backups[i]->refreshErrTime + 10800 < now || backups[i]->refreshErrTime > now + 10800))
		{
			if (pick == -1 || backups[i]->refreshTime < backups[pick]->refreshTime ||
					(backups[i]->refreshTime == backups[pick]->refreshTime &&
					backups[i]->refreshErrTime < backups[pick]->refreshErrTime))
				pick = i;
		}
	}
	if (pick != -1)
		res = refreshOneBackup(*backups[pick]);
	return res;
}

int Aresq::refreshOneBackup(Backup &backup)
{
	std::unique_lock<Spinlock> refreshLock(refreshMutex, std::try_to_lock);
	if (!refreshLock.owns_lock())
		PELOG_ERROR_RETURN((PLV_ERROR, "Aresq already running\n"), CONFLICT);
	auto recordErr = [&]() {
		backup.refreshErrTime = (uint32_t)time64(NULL);
		regi.set(backup.name.c_str(), "refreshErrTime", backup.refreshErrTime);
	};

	int res = 0;
	PELOG_LOG((PLV_INFO, "refreshOneBackup (%s): Begin\n", backup.name.c_str()));
	res = remote->connect();
	if (res != OK)
		PELOG_ERROR_RETURN((PLV_ERROR, "refreshOneBackup: connect failed (%d)\n", res), DISCONNECTED);
	ResGuard<Remote> remote_guard(remote.get(), [](Remote *r) { r->disconnect(); });

	// Load refreshStep
	std::string stepName;
	std::vector<std::string> step;
	std::string stepstr = regi.get(backup.name.c_str(), "refreshStep");
	{
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
		if (!stepstr.empty() && regi.set(backup.name.c_str(), "refreshStep", "") != 0)	// clear the refreshStep record
		{
			recordErr();
			PELOG_ERROR_RETURN((PLV_ERROR, "clear refreshStep failed\n"), EINTERNAL);
		}
	}
	// validate stepname
	if (!stepName.empty() && stepName != backup.name)
	{
		PELOG_LOG((PLV_ERROR, "Invalid refresh step name %s\n", stepName.c_str()));
		stepName.clear();
		step.clear();
	}
	if (!stepName.empty())
		PELOG_LOG((PLV_INFO, "Loaded step name %s\n", stepstr.c_str()));

	Root &root = backup.root;
	std::unique_lock<std::mutex> rootRefreshLock;
	res = root.startRefresh(&step, rootRefreshLock);
	if (res != OK)
		PELOG_ERROR_RETURN((PLV_ERROR, "refreshOneBackup (%s): start failed %d\n", backup.name.c_str(), res), EINTERNAL);
	Root::Action action;
	int state = 0;
	while (true)	// refreshSteps
	{
		if (action.type != Root::Action::BREAK)
			PELOG_LOG((PLV_DEBUG, "refreshStep\n"));
		res = root.refreshStep(state, action);
		if (res != AGAIN && res != OK)
		{
			recordErr();
			PELOG_ERROR_RETURN((PLV_ERROR, "refreshOneBackup (%s): step failed %d\n", backup.name.c_str(), res), EINTERNAL);
		}
		if (res == OK)
			break;
		state = root.perform(action, remote.get());
		if (state != OK)
			PELOG_LOG((PLV_ERROR, "refreshOneBackup (%s): perform failed %d\n", backup.name.c_str(), res));
		if (state == DISCONNECTED)
			break;
		if (stopFlag)
		{
			PELOG_LOG((PLV_INFO, "refreshOneBackup (%s): Stopped\n", backup.name.c_str()));
			break;
		}
		if (revisionMgr.hasReady())
		{
			PELOG_LOG((PLV_INFO, "refreshOneBackup (%s): Paused\n", backup.name.c_str()));
			break;
		}
	}	// while (true)	// refreshSteps

	if (res == OK)
	{
		backup.refreshTime = (uint32_t)time64(NULL);
		regi.set(backup.name.c_str(), "refreshTime", backup.refreshTime);
		PELOG_LOG((PLV_INFO, "refreshOneBackup (%s): Finished\n", backup.name.c_str()));
	}
	else if (res == AGAIN)	// not finished, save progress
	{
		root.refreshSave(&step);
		if (!step.empty())
		{
			std::string stepstr(backup.name);
			for (const std::string &stepdir : step)
				stepstr.append("/").append(stepdir);
			PELOG_LOG((PLV_INFO, "refreshBackup: save step %s\n", stepstr.c_str()));
			if (regi.set(backup.name.c_str(), "refreshStep", stepstr.c_str()) != 0)
				PELOG_LOG((PLV_ERROR, "record refreshStep failed\n"));
		}
	}

	AuAssert(root.verify());
	return res;
}

// Mode 2: Continuous monitoring & incremental update
//
//   Monitor  ->  OS filesystem events -> revisionMgr.submit()
//          |  (revisionMgr.eventQueue)
//   RevisionMgr  ->  organize & 2-min quiesce
//          |  (revisionMgr.items)
//   executor     ->  revisionMgr.popReady() -> executeItem() -> Remote
int Aresq::run()
{
	revisionMgr.start(backups, ignore.get(), monitorCommitDelay);

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
	return 0;
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
		if (!revisionMgr.waitReady(stopFlag, std::chrono::seconds(idleTimeout)))
		{
			if (stopFlag)
				break;
			onIdle();
			continue;
		}

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
			PELOG_LOG((PLV_INFO, "Execute %s (%s): %s\n", item->opname(), backups[item->ibackup]->name.c_str(), item->file1.c_str()));
			int eret = executeItem(*item);
			if (eret == OK)
			{
				// Succeeded, queue parent dir for refresh
				if (item->op != TaskFile::TF_REFRESH)
				{
					submitRefreshParent(item->ibackup, item->file1.c_str(), false);
					if (item->op == TaskFile::TF_REN)
						submitRefreshParent(item->ibackup, item->file2.c_str(), false);
				}
				continue;
			}
			PELOG_LOG((PLV_ERROR, "Execute failed (%d) %s: %s\n", eret, item->opname(), item->file1.c_str()));
			if (eret != DISCONNECTED)
				++item->failnum;
			if (item->failnum <= 3)
			{
				revisionMgr.putBack(std::move(item));
				stopFlag.wait_for(std::chrono::seconds(10));
			}
			else
			{
				PELOG_LOG((PLV_ERROR, "Convert failed task into parent refresh %s: %s\n", item->opname(), item->file1.c_str()));
				submitRefreshParent(item->ibackup, item->file1.c_str(), true);
				if (item->op == TaskFile::TF_REN)
					submitRefreshParent(item->ibackup, item->file2.c_str(), true);
			}
			break;
		}

		remote->disconnect();
	}
	PELOG_LOG((PLV_INFO, "Aresq::executorProc done\n"));
}

int Aresq::submitRefreshParent(int ibackup, const char *path, bool force)
{
	// submit refresh for parent dir
	if (!path || !*path || path[0] == '/' && path[1] == 0)
		return 0;
	size_t parentlen = pathDirLen(path, strlen(path));
	std::string dir(path, parentlen);
	return revisionMgr.submit(std::make_unique<TaskFile>(ibackup, TaskFile::TF_REFRESH, dir.c_str(), "", force));
}


// Dispatch a single ready task to the appropriate Root operation via Remote.
int Aresq::executeItem(TaskFile &item)
{
	Backup &bk = *backups[item.ibackup];
	Root &root = bk.root;
	const char *file1 = item.file1.c_str();
	const char *file2 = item.file2.c_str();

	int res = OK;
	Root::Action action;
	action.keephist = bk.keephist;
	action.name.scopyFrom(file1);
	switch (item.op)
	{
	case TaskFile::TF_NEW:
		action.type = item.filetype == TaskFile::FT_DIR ? Root::Action::ADDDIR : Root::Action::ADDFILE;
		res = root.perform(action, remote.get());
		break;
	case TaskFile::TF_MOD:
		action.type = Root::Action::MODFILE;
		res = root.perform(action, remote.get());
		break;
	case TaskFile::TF_DEL:
		action.type = item.filetype == TaskFile::FT_DIR ? Root::Action::DELDIR : Root::Action::DELFILE;
		res = root.perform(action, remote.get());
		if (res == NOTFOUND)
			res = OK;	// treat not found as success for delete
		break;
	case TaskFile::TF_REN:
		action.type = Root::Action::RENAME;
		action.dst.scopyFrom(file2);
		res = root.perform(action, remote.get());
		break;
	case TaskFile::TF_REFRESH:
		break;	// will perform later with item.recur cases
	default:
		PELOG_ERROR_RETURN((PLV_ERROR, "executeItem: unexpected op %s\n", item.opname()), -1);
	}
	if (res != OK || (item.op != TaskFile::TF_REFRESH && !item.recur))
		return res;	// not refresh and not recur, finish

	// TF_REFRESH or item.recur, do refresh
	const char *refreshPath = item.op == TaskFile::TF_REN ? file2 : file1;
	if (item.op == TaskFile::TF_REFRESH && !item.force)	// if not force, do not refresh if just refreshed recently
	{
		uint32_t rtime = root.getRecordTime(refreshPath);
		uint32_t now = (uint32_t)time64(NULL);
		if (rtime >= now - 3600 && rtime <= now + 3600)
			PELOG_ERROR_RETURN((PLV_DEBUG, "Skip refresh dir %s\n", refreshPath), OK);
	}
	PELOG_LOG((PLV_VERBOSE, "Refresh dir %s (%s): %s\n", item.recur ? "RECUR" : "NORECUR", bk.name.c_str(), refreshPath));
	std::unique_lock<std::mutex> rootRefreshLock;
	res = root.startRefreshSingle(refreshPath, remote.get(), item.recur, rootRefreshLock);
	action.type = Root::Action::NONE;
	int state = OK;
	while (res == AGAIN)
	{
		res = root.refreshStep(state, action);
		if (res == AGAIN)
		{
			state = root.perform(action, remote.get());
			if (stopFlag)
				res = ECANCELE;
		}
	}
	AuAssert(root.verify());
	return res;
}

int Aresq::onDataChange(std::unique_ptr<TaskFile> task)
{
	if (task->file1 == "aresqignore")
		ignore->setupdated();
	return 0;
}

int Aresq::onIdle()
{
	// compact records
	for (std::unique_ptr<Backup> &backup : backups)
	{
		if (!backup->root.loaded())
			continue;
		if (backup->compactTime + 86400 < time64(NULL) || backup->compactTime > time64(NULL) + 86400)
		{
			int res = backup->root.compact(1024 * 512);
			if (res == OK)
			{
				backup->compactTime = (uint32_t)time64(NULL);
				regi.set(backup->name.c_str(), "compactTime", backup->compactTime);
			}
			else
				PELOG_LOG((PLV_ERROR, "Compact backup %s failed: %d\n", backup->name.c_str(), res));
		}
	}

	// root refresh
	return refreshDyn();
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
