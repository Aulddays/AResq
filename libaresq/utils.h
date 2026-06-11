#pragma once

#include <atomic>
#include <mutex>
#include <condition_variable>

class Spinlock
{
	std::atomic_flag m_ = ATOMIC_FLAG_INIT;
public:
	bool try_lock() noexcept { return !m_.test_and_set(std::memory_order_acquire); }
	void lock()     noexcept { while (!try_lock()) {} }
	void unlock()   noexcept { m_.clear(std::memory_order_release); }
};

// Manual-reset waitable event
class Event
{
	mutable std::mutex mtx;
	std::condition_variable cv;
	bool signaled = false;
public:
	void set()	// set signaled
	{
		{ std::lock_guard<std::mutex> lk(mtx); signaled = true; }
		cv.notify_all();
	}
	void reset()	// set unsignaled
	{
		std::lock_guard<std::mutex> lk(mtx);
		signaled = false;
	}
	bool get() const	// query signaled state
	{
		std::lock_guard<std::mutex> lk(mtx);
		return signaled;
	}
	Event &operator=(bool v) { v ? set() : reset(); return *this; }
	operator bool() const { return get(); }
	void wait()	// wait until signaled
	{
		std::unique_lock<std::mutex> lk(mtx);
		cv.wait(lk, [this]{ return signaled; });
	}
	// wait until signaled or timed out. Returns true if signaled, false if timed out.
	template<class Rep, class Period>
	bool wait_for(const std::chrono::duration<Rep, Period> &timeout) {
		std::unique_lock<std::mutex> lk(mtx);
		return cv.wait_for(lk, timeout, [this]{ return signaled; });
	}
};

inline std::string realpath(const char *path)
{
#ifdef _WIN32
	// convert to wchar
	wchar_t wbuf[MAX_PATH];
	if (MultiByteToWideChar(CP_ACP, 0, path, -1, wbuf, MAX_PATH) == 0)
		PELOG_ERROR_RETURN((PLV_ERROR, "realpath invalid dir: %s\n", path), path);
	// get full path
	wchar_t *fullbuf = _wfullpath(NULL, wbuf, MAX_PATH);
	if (!fullbuf)
		PELOG_ERROR_RETURN((PLV_ERROR, "realpath parse dir failed: %s\n", path), path);
	wcscpy(wbuf, fullbuf);
	free(fullbuf);
	// convert to utf8
	char buf[MAX_PATH];
	if (WideCharToMultiByte(CP_UTF8, 0, wbuf, -1, buf, MAX_PATH, NULL, NULL) == 0)
		PELOG_ERROR_RETURN((PLV_ERROR, "realpath convert dir failed: %s\n", path), path);
	char *p = buf;
	// '\\' -> '/'
	for (p = buf; *p; ++p)
		if (*p == '\\')
			*p = '/';
	// trim trailing '/'
	for (p--; p >= buf && *p == '/'; p--)
		*p = 0;
	return std::string(buf);
#else
	static_assert(false, "NOT IMPLEMENTED");
#endif
}