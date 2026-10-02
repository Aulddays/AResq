// aresqc.cpp : Defines the entry point for the console application.
//

#include "stdafx.h"

#include "libaresq/Root.h"

#include <stdio.h>
#include <vector>
#include <memory>
#include <direct.h>
#include <signal.h>

#include "libaresq/Aresq.h"
#include "libaresq/Remote.h"
#include "libaresq/resguard.h"

#define LIBCONFIG_STATIC
#include "libconfig/libconfig.h"

#ifdef _MSC_VER
#	define chdir _chdir
#endif

int doencdec(bool enc);
int testremote(const char *configfile);

Aresq aresq;

#ifdef _WIN32
BOOL WINAPI sighdl(DWORD code)
{
	switch (code)
	{
	case CTRL_C_EVENT:
	case CTRL_BREAK_EVENT:
	case CTRL_CLOSE_EVENT:
	case CTRL_LOGOFF_EVENT:
	case CTRL_SHUTDOWN_EVENT:
		PELOG_LOG((PLV_INFO, "Stopping\n"));
		aresq.stop();
		return TRUE;
	default:
		return FALSE;
	}
}
#else
void sighdl(int code)
{
	if (code == SIGINT || code == SIGTERM || code == SIGABRT)
	{
		PELOG_LOG((PLV_INFO, "Stopping\n"));
		aresq.stop();
	}
	else if (code == SIGUSR1)
	{
		PELOG_LOG((PLV_TRACE, "SIGUSR1 received\n"));
		aresq.dumpStatus();
	}
}
#endif

int main(int argc, char *argv[])
{
	if (argc == 2 && (strcmp(argv[1], "-e") == 0 || strcmp(argv[1], "-d") == 0))
		return doencdec(argv[1][1] == 'e');

	if (strcmp(argv[1], "-t") == 0)
	{
		if (argc < 3)
			PELOG_ERROR_RETURN((PLV_ERROR, "Usage: aresqc -t config_file\n"), -1);
		return testremote(argv[2]);
	}

	std::string datadir = ".";
	if (argc > 1)
		datadir = argv[1];

	if (aresq.init(datadir) != 0)
		PELOG_ERROR_RETURN((PLV_ERROR, "init failed\n"), -1);

#ifdef _WIN32
	SetConsoleCtrlHandler(sighdl, TRUE);
#else
	signal(SIGINT, sighdl);
	signal(SIGTERM, sighdl);
	signal(SIGABRT, sighdl);
	signal(SIGUSR1, sighdl);
#endif

	//return aresq.refreshAll();
	return aresq.run();
}

int doencdec(bool enc)
{
	char buf[1024];
	while (fgets(buf, 1024, stdin))
	{
		char *pos = strpbrk(buf, "\r\n");
		if (pos)
			*pos = 0;
		std::string s = enc ? Aresq::encpwd(buf) : Aresq::decpwd(buf);
		printf("%s\n", s.c_str());
	}
	return 0;
}

int testremote(const char *configfile)
{
	PELOG_LOG((PLV_INFO, "Testing remote config: %s\n", configfile));

	config_t config;
	config_init(&config);
	ResGuard<config_t> config_guard(&config, [](config_t *c) { config_destroy(c); });

	if (CONFIG_FALSE == config_read_file(&config, configfile))
		PELOG_ERROR_RETURN((PLV_ERROR, "Error loading config file (%s : %d): %s\n",
			configfile, config_error_line(&config), config_error_text(&config)), -1);

	std::unique_ptr<Remote> remote = Remote::fromConfig(&config);
	if (!remote)
		PELOG_ERROR_RETURN((PLV_ERROR, "Failed to create remote from config.\n"), -1);

	PELOG_LOG((PLV_INFO, "Connecting...\n"));
	int ret = remote->connect();
	if (ret != 0)
		PELOG_ERROR_RETURN((PLV_ERROR, "connect() returned %d\n", ret), -1);

	if (!remote->isConnected())
	{
		PELOG_LOG((PLV_ERROR, "connect() returned 0 but isConnected() is false\n"));
		remote->disconnect();
		return -1;
	}
	PELOG_LOG((PLV_INFO, "SUCCESS: Remote is connected.\n"));

	remote->disconnect();
	PELOG_LOG((PLV_INFO, "Disconnected.\n"));
	return 0;
}