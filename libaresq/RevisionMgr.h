#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>
#include <string>
#include "tasking.h"
#include "utils.h"

struct Backup;
class AresqIgnore;

class RevisionMgr {
public:
	void start(const std::vector<std::unique_ptr<Backup>> &backups, AresqIgnore *ignore);

	// monitor: enqueue a raw fs event; returns -1 if organizeThrd has stopped
	int submit(std::unique_ptr<TaskFile> event);

	// executor: block until a ready item exists or stopFlag is set; returns true if ready
	bool waitReady(const Event &stopFlag);

	// executor: pop oldest ready item (by seq); nullptr if none ready
	std::unique_ptr<TaskFile> popReady();
	// executor: re-insert failed item
	void putBack(std::unique_ptr<TaskFile> task);

	// called after monitor stops: drains eventQueue then unblocks waitReady
	void notifyStop();
	void join();   // waits for organizeThrd to finish

private:
	TaskQueue eventQueue;
	std::thread organizeThrd;

	std::mutex mutex;
	std::condition_variable cv;

	const std::vector<std::unique_ptr<Backup>> *backups = nullptr;
	AresqIgnore *ignore = nullptr;
	std::vector<std::vector<std::unique_ptr<TaskFile>>> items;  // items[ibackup]

	void organizeproc();
	void process(std::unique_ptr<TaskFile> task);  // merge/dedup into items
	std::atomic<bool> stopped{false};

	// helpers called with mutex held
	bool hasReadyLocked() const;
	std::chrono::steady_clock::duration timeUntilReadyLocked() const;
};
