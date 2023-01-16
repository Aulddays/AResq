#pragma once

#include <string>
#include <vector>
#include <time.h>
#include <stdint.h>
#include <memory>

class IgnoreList;

class AresqIgnore
{
public:
	AresqIgnore();
	~AresqIgnore();
	bool isignore(const char *filename, bool isdir);

	int loadglobal(const char *filename, bool forcecreate = true);

private:
	std::unique_ptr<IgnoreList> grule;
	std::vector<std::unique_ptr<IgnoreList>> rules;
};