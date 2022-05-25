// aresqc.cpp : Defines the entry point for the console application.
//

#include "stdafx.h"

#include "libaresq/Root.h"

#include <stdio.h>
#include <vector>
#include "libaresq/utfconv.h"
#include "libaresq/pe_log.h"

int main(int argc, char* argv[])
{
	Root tmp("records", "E:\\tmp");
	tmp.load();
	tmp.startRefresh();
	while (true)
	{
		PELOG_LOG((PLV_DEBUG, "refreshStep\n"));
		int res = tmp.refreshStep(0);
		if (res != 1 && res != 0)
			PELOG_ERROR_RETURN((PLV_ERROR, "refreshStep failed %d\n", res), -1);
		if (res == 0)
			break;
	}
	return 0;
}

