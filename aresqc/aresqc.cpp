// aresqc.cpp : Defines the entry point for the console application.
//

#include "stdafx.h"

#include "libaresq/Root.h"

#include <stdio.h>
#include <vector>
#include <memory>

#include "libaresq/Aresq.h"

int main(int argc, char* argv[])
{
	std::string datadir = ".";
	if (argc > 1)
		datadir = argv[1];

	Aresq aresq;
	if (aresq.init(datadir) != 0)
		PELOG_ERROR_RETURN((PLV_ERROR, "init failed\n"), -1);

	return aresq.run();
}

