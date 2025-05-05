#pragma once
#include <stdint.h>
#include <stddef.h>
#include <time.h>
#include <map>
#include <unordered_map>
#include <algorithm>
#include <vector>


namespace skynet_ext {
namespace ms_timer {

class Poller;
struct RequestAddTimer;

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

	TimerNode(uint32_t service_handle, int session, int count, uint32_t interval_ms, timespec *now);
	void OnTimeout();
};

class TimerPool {
public:
	TimerPool();
	~TimerPool();

	int CheckInit(Poller *poller, int id);
	void Release(Poller *poller);
	void AddTimer(Poller *poller, RequestAddTimer *request, timespec *now);
	void DelTimer(int session);
	void CheckTimeout(Poller *poller, timespec *now);

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
	int id_;
	int timer_fd;
	std::vector<TimerNode *> container;
	std::unordered_map<int, TimerNode *> timer_nodes;
};

} // namespace ms_timer
} // namespace skynet_ext