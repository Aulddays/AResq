#include "stdafx.h"
#include "Remote.h"
#include <map>
#include <string>
#include "RemoteSmb.h"


std::unique_ptr<Remote> Remote::fromConfig(const config_t *config)
{
	static const std::map<std::string, std::unique_ptr<Remote> (*)(const config_setting_t *config)> factories = {
		{ "smb", RemoteSmb::fromConfig },
		{ "dummy", RemoteDummy::fromConfig },
	};
	config_setting_t *cremote = config_lookup(config, "remote");
	if (!cremote)
		PELOG_ERROR_RETURN((PLV_ERROR, "remote setting not found\n"), nullptr);
	const char *type;
	if (CONFIG_TRUE != config_setting_lookup_string(cremote, "type", &type))
		PELOG_ERROR_RETURN((PLV_ERROR, "remote type not found\n"), nullptr);
	const auto &ifact = factories.find(type);
	if (ifact == factories.end())
		PELOG_ERROR_RETURN((PLV_ERROR, "remote type '%s' not supported\n", type), nullptr);
	return ifact->second(cremote);
}

