#include "stdafx.h"

#include "Aresq.h"
#include <random>

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

	// recorddir
	recorddir = datadir + "/records";

	// AresqIgnore
	ignore = std::make_unique<AresqIgnore>();
	if (ignore->loadglobal((datadir + '/' + "aresqignore").c_str(), true) != 0)
		PELOG_ERROR_RETURN((PLV_ERROR, "Global ignore file %s failed\n", (datadir + '/' + "aresqignore").c_str()), -1);

	// remote
	remote.reset(Remote::fromConfig(&config));
	if (!remote)
		PELOG_ERROR_RETURN((PLV_ERROR, "remote config error\n"), -1);

	// hist
	int keephist = true;
	config_lookup_bool(&config, "general.history", &keephist);

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
					(recorddir + '/' + name).c_str(), keephist != 0, ignore.get()) != 0)
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
		AuAssert(root.verify());
	}
	return 0;
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
	for (size_t i = 0; codebook[i]; ++i)
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