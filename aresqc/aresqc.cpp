// aresqc.cpp : Defines the entry point for the console application.
//

#include "stdafx.h"

#include "libaresq/Root.h"

#include <stdio.h>
#include <vector>
#include <memory>
#include <direct.h>

#include "libaresq/Aresq.h"

int doencdec(bool enc);

int main(int argc, char* argv[])
{
	if (argc == 2 && (strcmp(argv[1], "-e") == 0 || strcmp(argv[1], "-d") == 0))
		return doencdec(argv[1][1] == 'e');

	//*** DEBUG
	chdir("D:\\aresq");

	std::string datadir = ".";
	if (argc > 1)
		datadir = argv[1];

	Aresq aresq;
	if (aresq.init(datadir) != 0)
		PELOG_ERROR_RETURN((PLV_ERROR, "init failed\n"), -1);

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