#include "stdafx.h"
#include "Register.h"
#include "pe_log.h"
#include "resguard.h"

int Register::init(const char *filename)
{
	// open
	int res = 0;
	if ((res = sqlite3_open_v2(filename, &pdb, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX, NULL)) != SQLITE_OK)
		PELOG_ERROR_RETURN((PLV_ERROR, "register open failed: %d %s\n", res, sqlite3_errmsg(pdb)), res);

	// prepare table
	const char *sql = "CREATE TABLE IF NOT EXISTS Conf ([key] TEXT PRIMARY KEY NOT NULL, value)";
	if ((res = sqlite3_exec(pdb, sql, NULL, NULL, NULL)) != SQLITE_OK)
		PELOG_ERROR_RETURN((PLV_ERROR, "register init failed: %d %s\n", res, sqlite3_errmsg(pdb)), res);

	return 0;
}

int getvalue(void *p, int num, char **val, char **name)
{
	std::string &res = *(std::string *)p;
	if (num >= 1)
		res = val[0];
	return 0;
}

std::string Register::get(const char *key)
{
	ResGuard<char, decltype(&sqlite3_free)> sql(sqlite3_mprintf("SELECT value FROM Conf WHERE key=%Q", key), sqlite3_free);
	PELOG_LOG((PLV_DEBUG, "SQL: %s\n", sql.get()));
	std::string value;
	int res = 0;
	if ((res = sqlite3_exec(pdb, sql, getvalue, &value, NULL)) != SQLITE_OK)
		PELOG_ERROR_RETURN((PLV_ERROR, "Register get failed (%s): %d %s\n", key, res, sqlite3_errmsg(pdb)), "");
	return value;
}

std::string Register::get(const char *bkname, const char *key)
{
	return get(bkkey(bkname, key).c_str());
}

int Register::geti(const char *key, int defval)
{
	std::string value = get(key);
	if (value.empty())
		return defval;
	return atoi(value.c_str());
}

int Register::geti(const char *bkname, const char *key, int defval)
{
	return geti(bkkey(bkname, key).c_str(), defval);
}

int Register::set(const char *key, const char *value)
{
	ResGuard<char, decltype(&sqlite3_free)> sql(sqlite3_mprintf("INSERT OR REPLACE INTO Conf (key, value) VALUES (%Q, %Q)", key, value), sqlite3_free);
	PELOG_LOG((PLV_DEBUG, "SQL: %s\n", sql.get()));
	int res = 0;
	if ((res = sqlite3_exec(pdb, sql, NULL, NULL, NULL)) != SQLITE_OK)
		PELOG_ERROR_RETURN((PLV_ERROR, "Register set failed (%s): %d %s\n", key, res, sqlite3_errmsg(pdb)), res);
	return 0;
}

int Register::set(const char *bkname, const char *key, const char *value)
{
	return set(bkkey(bkname, key).c_str(), value);
}

int Register::set(const char *key, int val)
{
	ResGuard<char, decltype(&sqlite3_free)> sql(sqlite3_mprintf("INSERT OR REPLACE INTO Conf (key, value) VALUES (%Q, %d)", key, val), sqlite3_free);
	PELOG_LOG((PLV_DEBUG, "SQL: %s\n", sql.get()));
	int res = 0;
	if ((res = sqlite3_exec(pdb, sql, NULL, NULL, NULL)) != SQLITE_OK)
		PELOG_ERROR_RETURN((PLV_ERROR, "Register set failed (%s): %d %s\n", key, res, sqlite3_errmsg(pdb)), res);
	return 0;
}

int Register::set(const char *bkname, const char *key, int val)
{
	return set(bkkey(bkname, key).c_str(), val);
}
