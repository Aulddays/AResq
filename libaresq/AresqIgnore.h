#pragma once

#include <string>
#include <vector>
#include <time.h>
#include <stdint.h>
#include <memory>
#include <atomic>

class IgnoreList;

class AresqIgnore
{
public:
	AresqIgnore();
	~AresqIgnore();
	bool isignore(const char *filename, bool isdir);
	bool isignore_p(const char *filename, bool isdir);	// test filename and all parent dirs

	int loadglobal(const char *filename, bool forcecreate = true);

	void setupdated();

private:
	std::unique_ptr<IgnoreList> grule;
	std::vector<std::unique_ptr<IgnoreList>> rules;
	std::atomic<bool> updated{ false };
};