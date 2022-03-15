// aresqc.cpp : Defines the entry point for the console application.
//

#include "stdafx.h"

#include "libaresq/record.h"

#include <stdio.h>
#include <vector>

int main(int argc, char* argv[])
{
	printf("size %d\n", sizeof(RecordItem));
	printf("size10 %d\n", sizeof(RecordItem[10]));
	std::vector<RecordItem> files(10);
	printf("%p %p %p\n", &files[0], &files[1], &files[2]);
	return 0;
}

