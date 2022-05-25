#pragma once

#include <vector>
#include <deque>
#include "record.h"
#include "auto_buf.hpp"
#include "fsadapter.h"

class Root
{
public:
	Root(const char *recpath, const char *root);
	~Root();

	int load();

	int startRefresh();
	int refreshStep(int state);

private:
	std::string root;
	abuf<NCHART> ncroot;
	std::string recpath;
	std::vector<RecordItem> _records;
	std::vector<char> _rname;
	std::vector<HistItem> _hists;

	struct RefreshIter
	{
		uint32_t rid = 0;
		abufchar name;
		abufchar path;	// full path
		enum
		{
			INIT,
			DELFILE,
			DELDIR,
			FILE,
			DIR,
			RECUR,
			REDOUPPER,
			RETURN,
		} stage = INIT;
		uint32_t prog = 0;
		std::vector<FsItem> files;
		std::vector<FsItem> dirs;
	};
	std::deque<RefreshIter> restate;
	uint32_t reid;
	int refreshBuildPath(abuf<char> &path);

	int init();
	struct RootStat
	{
		uint32_t nfile = 0;
		uint32_t ndir = 0;
		uint32_t ndelf = 0;
		uint32_t ndeld = 0;
		uint32_t nrecy = 0;
	};
	bool verifyrec(RootStat *stat=NULL) const;

};

