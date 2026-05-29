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