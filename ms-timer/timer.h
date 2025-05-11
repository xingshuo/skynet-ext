#pragma once
#include <stdint.h>
#include <stddef.h>
#include <time.h>
#include <map>
#include <unordered_map>
#include <algorithm>
#include <vector>

extern "C" {
#include "skynet.h"
#include "skynet_server.h"
#include "skynet_mq.h"
}

namespace skynet_ext {
namespace ms_timer {

class Poller;
struct RequestAddTimer;
struct RequestDelTimer;

enum TimerState {
	PENDING,
	CANCELLED
};

struct TimerNode {
	uint32_t service_handle;
	int session;
	int count; // > 0 有限次
	uint32_t interval_ms;
	timespec timeout;
	TimerState state;

	TimerNode(const RequestAddTimer *request, const timespec *now);
	void OnTimeout(const timespec *now);

	static uint64_t HashKey(uint32_t service_handle, int session) {
		return (((uint64_t)service_handle) << 32) | (uint64_t)session;
	};
};

class TimerPool {
public:
	void AddTimer(Poller *poller, const RequestAddTimer *request, const timespec *now);
	void DelTimer(Poller *poller, const RequestDelTimer *request);
	void CheckTimeout(Poller *poller, const timespec *now);
	void Release(const Poller *poller);

private:
	struct Comparator {
		bool operator()(const TimerNode *a, const TimerNode *b)
		{
			long ret = a->timeout.tv_sec - b->timeout.tv_sec;
			if (ret == 0) {
				return a->timeout.tv_nsec > b->timeout.tv_nsec;
			}
			return ret > 0;
		}
	};
	void pushHeap(TimerNode *node) {
		container.push_back(node);
		std::push_heap(container.begin(), container.end(), Comparator());
	}

	void popHeap() {
		std::pop_heap(container.begin(), container.end(), Comparator());
		container.pop_back();
	}

private:
	std::vector<TimerNode *> container;
	std::unordered_map<uint64_t, TimerNode *> timer_nodes;
};

} // namespace ms_timer
} // namespace skynet_ext