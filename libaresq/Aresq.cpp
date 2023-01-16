#include "stdafx.h"

#include "Aresq.h"

#define LIBCONFIG_STATIC
#include "libconfig/libconfig.h"

#pragma comment(lib, "Ws2_32.lib")

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

int Aresq::init(const char *conffile)
{
	// load conf file
	configext_t config;
	if (CONFIG_FALSE == config_read_file(&config, conffile))
	{
		PELOG_ERROR_RETURN((PLV_ERROR, "Error loading config file (line %d): %s\n",
			config_error_line(&config), config_error_text(&config)), -1);
	}

	// datadir
	{
		const char *dir = NULL;
		if (!config_lookup_string(&config, "general.datadir", &dir) || !dir || !*dir)
			PELOG_ERROR_RETURN((PLV_ERROR, "general.datadir config not found\n"), -1);
		datadir = dir;
	}

	// AresqIgnore
	ignore = std::make_unique<AresqIgnore>();
	if (ignore->loadglobal((datadir + '/' + "aresqignore").c_str(), true) != 0)
		PELOG_ERROR_RETURN((PLV_ERROR, "Global ignore file %s failed\n", (datadir + '/' + "aresqignore").c_str()), -1);

	// remote
	remote.reset(Remote::fromConfig(&config));
	if (!remote)
		PELOG_ERROR_RETURN((PLV_ERROR, "remote config error\n"), -1);

	// backups
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

			backups.emplace_back(new Backup);
			backups.back()->id = (int)backups.size() - 1;
			backups.back()->name = name;
			backups.back()->dir = path;
			if (backups.back()->root.load(backups.back()->id, name, path,
					(datadir + '/' + name).c_str(), ignore.get()) != 0)
				PELOG_ERROR_RETURN((PLV_ERROR, "Init ackup idx(%d) %s failed\n", i, name), -1);
		}
	}

	return 0;
}

int Aresq::run()
{
	for (std::unique_ptr<Backup> &backup : backups)
	{
		Root &root = backup->root;
		root.startRefresh();
		Root::Action action;
		int state = 0;
		while (true)
		{
			PELOG_LOG((PLV_DEBUG, "refreshStep\n"));
			int res = root.refreshStep(state, action);
			if (res != 1 && res != 0)
				PELOG_ERROR_RETURN((PLV_ERROR, "refreshStep failed %d\n", res), -1);
			if (res == 0)
				break;
			state = root.perform(action, remote.get());
		}
	}
	return 0;
}