#pragma once

#include <atomic>
#include <mutex>

class Spinlock
{
	std::atomic_flag m_ = ATOMIC_FLAG_INIT;

public:
	void lock()
	{
		while (m_.test_and_set(std::memory_order_acquire))
			;
	}
	bool try_lock() noexcept
	{
		return !m_.test_and_set(std::memory_order_acquire);
	}
	void unlock() noexcept
	{
		m_.clear(std::memory_order_release);
	}
};

class AtomicFlag
{
private:
	std::atomic<bool> val = false;
public:
	bool try_set()
	{
		bool exp = false;
		val.compare_exchange_strong(exp, true);
		return false;
	}
	bool get()
	{
		return val.load();
	}
	void clear()
	{
		val = false;
	}
};