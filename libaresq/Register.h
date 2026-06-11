#pragma once
#include <string>
#include "libsqlite3/sqlite3.h"

static_assert(SQLITE_OK == 0, "Incompatible sqlite");

class Register
{
public:
	Register() { }
	~Register() { close(); }

	int init(const char *filename);

	int close() { if (pdb) return sqlite3_close_v2(pdb); pdb = NULL;  return 0; }

	std::string get(const char *key);
	std::string get(const char *bkname, const char *key);
	int geti(const char *key, int defval = 0);
	int geti(const char *bkname, const char *key, int defval = 0);
	int set(const char *key, const char *value);
	int set(const char *bkname, const char *key, const char *value);
	int set(const char *key, int val);
	int set(const char *bkname, const char *key, int val);

private:
	sqlite3 *pdb = NULL;
	std::string bkkey(const char *bkname, const char *key) { return std::string(bkname) + '/' + key; }
};

