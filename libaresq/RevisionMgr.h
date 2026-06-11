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
class Aresq;

class RevisionMgr {
public:
	RevisionMgr(Aresq *aresq) : aresq(aresq) {}

	void start(const std::vector<std::unique_ptr<Backup>> &backups, AresqIgnore *ignore, int commitDelay);

	// monitor: enqueue a raw fs event; returns -1 if organizeThrd has stopped
	int submit(std::unique_ptr<TaskFile> event);

	// executor: returns true if a ready item exists
	bool hasReady() const { std::lock_guard<std::mutex> lk(mutex); return hasReadyLocked(); }

	// executor: block until a ready item exists, stopFlag is set, or maxWait expires; returns true if ready
	bool waitReady(const Event &stopFlag,
		std::chrono::steady_clock::duration maxWait = std::chrono::steady_clock::duration::max());

	// executor: pop oldest ready item (by seq); nullptr if none ready
	std::unique_ptr<TaskFile> popReady();
	// executor: re-insert failed item
	void putBack(std::unique_ptr<TaskFile> task);

	// called after monitor stops: drains eventQueue then unblocks waitReady
	void notifyStop();
	void join();   // waits for organizeThrd to finish

private:
	Aresq *aresq;

	TaskQueue eventQueue;
	std::thread organizeThrd;

	mutable std::mutex mutex;
	std::condition_variable cv;

	const std::vector<std::unique_ptr<Backup>> *backups = nullptr;
	AresqIgnore *ignore = nullptr;
	std::chrono::steady_clock::duration commitDelay = std::chrono::seconds(120);
	std::vector<std::vector<std::unique_ptr<TaskFile>>> items;  // items[ibackup]

	void organizeproc();
	void process(std::unique_ptr<TaskFile> task);  // merge/dedup into items
	std::atomic<bool> stopped{false};

	// helpers called with mutex held
	bool hasReadyLocked() const;
	std::chrono::steady_clock::duration timeUntilReadyLocked() const;
};
