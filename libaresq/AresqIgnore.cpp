#include "stdafx.h"
#include "AresqIgnore.h"
#include "fsadapter.h"
#include "pe_log.h"

class IgnoreList
{
public:
	IgnoreList();
	~IgnoreList();

	int load(const char *ignorefilename);
	int update(bool force = false);
	// >0: ignore, <0: keep and stop testing, 0: not ignore but continue testing if there are more lists
	int isignore(const char *filename, bool isdir);
private:
	struct Pattern
	{
		std::string pat;
		bool neg = false;
		bool dir = false;
	};
	std::vector<Pattern> patterns;
	std::string filename;
	uint64_t filetime = 0;
	uint64_t updatetime = 0;
};

IgnoreList::IgnoreList()
{
}


IgnoreList::~IgnoreList()
{
}

int IgnoreList::load(const char *ignorefilename)
{
	uint64_t ftime = 0;
	uint64_t fsize = 0;
	if (getFileAttr("", ignorefilename, strlen(ignorefilename), ftime, fsize) != 0)
		PELOG_ERROR_RETURN((PLV_ERROR, "Cannot access %s\n", ignorefilename), -1);
	filename = ignorefilename;
	update(true);
	return 0;
}

int IgnoreList::update(bool force /*= false*/)
{
	uint64_t ftime = 0;
	uint64_t fsize = 0;
	if (getFileAttr("", filename.c_str(), filename.length(), ftime, fsize) != 0)
	{
		patterns.clear();
		PELOG_ERROR_RETURN((PLV_ERROR, "Cannot access %s\n", filename.c_str()), -1);
	}
	if (!force && ftime - 1 <= filetime && ftime + 1 >= filetime)
		PELOG_LOG_RETURN((PLV_VERBOSE, "%s not modified\n", filename.c_str()), 0);

	updatetime = time64(NULL);
	FileHandle fp = OpenFile(filename.c_str(), _NCT("r"));
	if (!fp)
		PELOG_ERROR_RETURN((PLV_ERROR, "Cannot access %s\n", filename.c_str()), -2);
	filetime = ftime;
	patterns.clear();
	char buf[1024];
	while (fgets(buf, 1024, fp))
	{
		bool neg = false, dir = false;
		char *pb = NULL, *pe = NULL;
		for (pb = buf; *pb > 0 && isspace(*pb); ++pb)
			;
		if (!*pb || *pb == '#')
			continue;
		if (*pb == '!')
		{
			neg = true;
			++pb;
		}
		for (pe = pb; *pe; ++pe)
			;
		for (--pe; *pe > 0 && isspace(*pe); --pe)
			;
		++pe;
		if (pe[-1] == '/')
		{
			dir = true;
			--pe;
		}
		if (pe <= pb)
			continue;
		patterns.emplace_back();
		patterns.back().pat.assign(pb, pe);
		patterns.back().dir = dir;
		patterns.back().neg = neg;
	}

	return 0;
}

bool gitignore_glob_match(const std::string &text, const std::string &glob);

// >0: ignore, <0: keep and stop testing, 0: not ignore but continue testing if there are more lists
int IgnoreList::isignore(const char *filename, bool isdir)
{
	uint64_t curtime = time64(NULL);
	if (curtime > updatetime + 60 || curtime < updatetime - 60)
		update();
	updatetime = curtime;
	std::string sfilename(filename);
	for (auto &ipat = patterns.crbegin(); ipat != patterns.crend(); ++ipat)
	{
		if (ipat->dir && !isdir)
			continue;
		if (gitignore_glob_match(sfilename, ipat->pat))
			return ipat->neg ? 0 : 1;
	}
	return -1;
}


AresqIgnore::AresqIgnore() : grule(new IgnoreList)
{
}

AresqIgnore::~AresqIgnore()
{
}

int AresqIgnore::loadglobal(const char *filename, bool forcecreate)
{
	if (forcecreate)
		FileHandle fp = OpenFile(filename, _NCT("ab+"));
	return grule->load(filename);
}

bool AresqIgnore::isignore(const char *filename, bool isdir)
{
	return grule->isignore(filename, isdir) > 0;
}