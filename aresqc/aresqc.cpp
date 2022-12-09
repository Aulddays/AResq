// aresqc.cpp : Defines the entry point for the console application.
//

#include "stdafx.h"

#include "libaresq/Root.h"

#include <stdio.h>
#include <vector>
#include <memory>

#include "libaresq/fsadapter.h"
#include "libaresq/utfconv.h"
#include "libaresq/pe_log.h"
#include "libaresq/RemoteSmb.h"
#define LIBCONFIG_STATIC
#include "libconfig/libconfig.h"

#pragma comment(lib, "Ws2_32.lib")

struct configext_t : public config_t	// simple resource manager for config_t
{
	configext_t() { config_init(this); }
	~configext_t() { config_destroy(this); }
};

int main(int argc, char* argv[])
{
	configext_t config;
	if (CONFIG_FALSE == config_read_file(&config, "conf/aresq.conf"))
	{
		PELOG_ERROR_RETURN((PLV_ERROR, "Error loading config file (line %d): %s\n",
			config_error_line(&config), config_error_text(&config)), -1);
	}

	std::string datadir;
	{
		const char *dir = NULL;
		if (!config_lookup_string(&config, "general.datadir", &dir) || !dir || !*dir)
			PELOG_ERROR_RETURN((PLV_ERROR, "general.datadir config not found\n"), -1);
		datadir = dir;
	}

	std::unique_ptr<Remote> remote(Remote::fromConfig(&config));
	if (!remote)
		PELOG_ERROR_RETURN((PLV_ERROR, "remote config error\n"), -1);

	std::vector<std::unique_ptr<Root>> backups;
	{
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
			backups.emplace_back(new Root(name, path, (datadir + DIRSEP + name).c_str()));
			if (backups.back()->load() != 0)
				PELOG_ERROR_RETURN((PLV_ERROR, "Init ackup idx(%d) %s failed\n", i, name), -1);
		}
	}

	for (std::unique_ptr<Root> &backup : backups)
	{
		backup->startRefresh();
		Root::Action action;
		int state = 0;
		while (true)
		{
			PELOG_LOG((PLV_DEBUG, "refreshStep\n"));
			int res = backup->refreshStep(state, action);
			if (res != 1 && res != 0)
				PELOG_ERROR_RETURN((PLV_ERROR, "refreshStep failed %d\n", res), -1);
			if (res == 0)
				break;
			state = backup->perform(action, remote.get());
		}
	}

	return 0;
}

