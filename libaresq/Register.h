#pragma once
#include <string>
#include "sqlite3.h"

static_assert(SQLITE_OK == 0, "Incompatible sqlite");

class Register
{
public:
	Register() { }
	~Register() { close(); }

	int init(const char *filename);

	int close() { if (pdb) return sqlite3_close_v2(pdb); pdb = NULL;  return 0; }

	std::string get(const char *key);
	int set(const char *key, const char *value);

private:
	sqlite3 *pdb = NULL;
};

