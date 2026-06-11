#pragma once

#include <time.h>
#include <deque>
#include <memory>
#include <mutex>
#include <condition_variable>
#include <assert.h>
#include <thread>
#include <chrono>

#if __cplusplus < 201402L && !defined(_MSC_VER)
namespace std {
template<typename T, typename... Args>
unique_ptr<T> make_unique(Args&&... args) {
    return unique_ptr<T>(new T(std::forward<Args>(args)...));
}
}
#endif

class Signal
{
public:
	void fire() { std::lock_guard<std::mutex> lk(m); active = true; cv.notify_all(); };

	template<class Rep, class Period> bool wait_for(const std::chrono::duration<Rep, Period> &rel_time)
	{
		std::unique_lock<std::mutex> lk(m);
		bool ret = cv.wait_for(lk, rel_time, [this] { return active; });
		active = false;
		return ret;
	}
private:
	std::mutex m;
	std::condition_variable cv;
	bool active = false;
};

class Task
{
public:
	Task() : time(::time(NULL)) { }
	virtual ~Task() { }
	time_t time;
	enum
	{
		TT_NONE,
		TT_TIMER,
		TT_FILE,
		TT_STOP,
	} tasktype = TT_NONE;
	int64_t seq = -1;
};

class TaskStop: public Task
{
public:
	TaskStop() { tasktype = TT_STOP; }
};

class TaskTimer : public Task
{
public:
	TaskTimer(uint64_t tick) : tick(tick) { tasktype = TT_TIMER; }
	uint64_t tick;
};

class TaskFile : public Task
{
public:
	~TaskFile() { }
	enum FileOP { TF_NONE, TF_NEW, TF_DEL, TF_MOD, TF_REN, TF_REN_SRC, TF_REN_DST, TF_REFRESH, TF_NUM };
	TaskFile(int ibackup_, FileOP op, const char *file1, const char *file2 = "", bool force = false) :
		op(op), file1(file1), file2(file2), ibackup(ibackup_), force(force),
		timeSteady(std::chrono::steady_clock::now()) { tasktype = TT_FILE; }
	static char * OpName[TF_NUM];
	FileOP op = TF_NONE;
	const char *opname() const
	{
		static const char * names[TF_NUM] = { "NONE", "NEW", "DEL", "MOD", "REN", "REN_SRC", "REN_DST", "REFRESH" };
		static_assert(sizeof(names) / sizeof(names[0]) == TF_NUM, "opnames not match");
		return names[op];
	}
	std::string file1;
	std::string file2;
	int ibackup = -1;
	enum FileType { FT_UNK, FT_FILE, FT_DIR };
	FileType filetype = FT_UNK;
	bool recur = false;
	bool force = false;
	int failnum = 0;

	std::chrono::steady_clock::time_point timeSteady;
};

class TaskQueue
{
public:
	TaskQueue() { }
	~TaskQueue() { }
	void stop() { putfront(std::make_unique<TaskStop>()); }
	void put(std::unique_ptr<Task> &&task)
	{
		std::lock_guard<std::mutex> lock(mutex);
		task->seq = seq++;
		tasks.push_back(std::move(task));
		cond.notify_all();
	}
	void putfront(std::unique_ptr<Task> &&task)
	{
		std::lock_guard<std::mutex> lock(mutex);
		task->seq = seq++;
		tasks.push_front(std::move(task));
		cond.notify_all();
	}
	std::unique_ptr<Task> get()
	{
		std::unique_lock<std::mutex> lock(mutex);
		cond.wait(lock, [this] { return !tasks.empty(); });
		assert(!tasks.empty() && lock.owns_lock());
		std::unique_ptr<Task> task = std::move(tasks.front());
		tasks.pop_front();
		return task;
	}
	std::unique_ptr<Task> tryget()
	{
		std::unique_lock<std::mutex> lock(mutex);
		if (tasks.empty())
			return NULL;
		std::unique_ptr<Task> task = std::move(tasks.front());
		tasks.pop_front();
		return task;
	}
	bool empty()
	{
		std::unique_lock<std::mutex> lock(mutex);
		return tasks.empty();
	}
private:
	std::deque<std::unique_ptr<Task>> tasks;
	std::mutex mutex;
	std::condition_variable cond;
	int64_t seq = 0;
};