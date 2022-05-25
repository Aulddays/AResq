// aresqc.cpp : Defines the entry point for the console application.
//

#include "stdafx.h"

#include "libaresq/Root.h"

#include <stdio.h>
#include <vector>
#include "libaresq/utfconv.h"

int main(int argc, char* argv[])
{
	Root tmp("records", "E:\\tmp");
	tmp.load();
	return 0;
}

