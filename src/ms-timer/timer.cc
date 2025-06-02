#include <fcntl.h>
#include <unistd.h>

#include "ms-timer/timer.h"
#include "ms-timer/poller.h"
#include "ms-timer/common.h"

namespace skynet_ext {
namespace ms_timer {

static void get_timeout(timespec *timeout, const timespec *now, int delta_ms) {
	*timeout = *now;
	timeout->tv_sec += delta_ms / 1000;
	timeout->tv_nsec += delta_ms % 1000 * 1000000;
	if (timeout->tv_nsec >= 1000000000) {
		timeout->tv_nsec -= 1000000000;
		timeout->tv_sec++;
	}
}

static int get_timeout_gap(timespec *gap, const timespec *now, const timespec *timeout) {
	if (now->tv_sec > timeout->tv_sec || (now->tv_sec == timeout->tv_sec && now->tv_nsec >= timeout->tv_nsec)) {
		// touch timeout at once
		gap->tv_sec = 0;
		gap->tv_nsec = 100000; // 0.1ms; Notice: 这里不能设置为0! 当timerfd_settime第三个参数it_value和it_interval都为0时, 会禁用定时器
		return 1;
	}
	gap->tv_sec = timeout->tv_sec - now->tv_sec;
	gap->tv_nsec = timeout->tv_nsec - now->tv_nsec;
	if (gap->tv_nsec < 0) {
		gap->tv_nsec += 1000000000;
		gap->tv_sec--;
	}
	return 0;
}

static inline int timeout_cmp(const timespec *a, const timespec *b) {
	int ret = a->tv_sec - b->tv_sec;
	if (ret == 0)
		ret = a->tv_nsec - b->tv_nsec;
	return ret;
}


TimerNode::TimerNode(const RequestAddTimer *request, const timespec *now) {
	this->state = TimerState::PENDING;
	this->service_handle = request->service_handle;
	this->session = request->session;
	this->count = request->count;
	this->interval_ms = request->interval_ms;
	get_timeout(&this->timeout, now, this->interval_ms);
}

void TimerNode::OnTimeout(const timespec *now) {
	// dispatch timeout message to register service
	skynet_message message;
	message.source = 0;
	message.session = this->session;
	message.data = NULL;
	message.sz = (size_t)PTYPE_MSTIMER << MESSAGE_TYPE_SHIFT;
	skynet_context_push(this->service_handle, &message);
}


void TimerPool::AddTimer(Poller *poller, const RequestAddTimer *request, const timespec *now) {
	uint64_t key = TimerNode::HashKey(request->service_handle, request->session);
	auto iter = timer_nodes.find(key);
	if (iter != timer_nodes.end()) {
		TimerNode* lastnode = iter->second;
		skynet_error(nullptr, "ms-timer: timer pool %d addtimer remove lastnode:(%u, %d) node:%p", poller->ID(), lastnode->service_handle, lastnode->session, lastnode);
		lastnode->state = TimerState::CANCELLED;
		lastnode->service_handle = 0;
		lastnode->session = 0; // skynet.call invalid session id
	}

	TimerNode *node = new TimerNode(request, now);
	timer_nodes[key] = node;
	pushHeap(node);
#if DEBUG_LOG_OUTPUT
	skynet_error(nullptr, "ms-timer: timer pool %d addtimer session:%d node:%p at (%ld, %ld)", poller->ID(), node->session, node, now->tv_sec, now->tv_nsec);
#endif
	TimerNode *top = container.front();
	timespec gap;
	get_timeout_gap(&gap, now, &top->timeout);
	poller->SetTimerFd(&gap);
}

void TimerPool::DelTimer(Poller *poller, const RequestDelTimer *request) {
	uint64_t key = TimerNode::HashKey(request->service_handle, request->session);
	auto iter = timer_nodes.find(key);
	if (iter == timer_nodes.end()) {
		skynet_error(nullptr, "ms-timer: timer pool %d deltimer unknown key:(%lu, %d)", poller->ID(), request->service_handle, request->session);
		return;
	}
	TimerNode* node = iter->second;
	node->state = TimerState::CANCELLED;
#if DEBUG_LOG_OUTPUT
	skynet_error(nullptr, "ms-timer: timer pool %d deltimer key:(%lu, %d) node:%p", poller->ID(), request->service_handle, request->session, node);
#endif
}

void TimerPool::CheckTimeout(Poller *poller, const timespec *now) {
	while (container.size() > 0) {
		TimerNode *node = container.front();
		uint64_t key = TimerNode::HashKey(node->service_handle, node->session);
		if (node->state == TimerState::CANCELLED) {
#if DEBUG_LOG_OUTPUT
			timespec curtime;
			clock_gettime(CLOCK_MONOTONIC, &curtime);
			skynet_error(nullptr, "ms-timer: poller %d on cancelled timer node %p at (%ld, %ld) curtime (%ld, %ld) dt:(%ld, %ld)", poller->ID(), node, now->tv_sec, now->tv_nsec, curtime.tv_sec, curtime.tv_nsec, curtime.tv_sec-now->tv_sec, curtime.tv_nsec-now->tv_nsec);
#endif
			popHeap();
			timer_nodes.erase(key);
			delete node;
			continue;
		}
		if (timeout_cmp(&node->timeout, now) > 0) {
			break;
		}
#if DEBUG_LOG_OUTPUT
		timespec curtime;
		clock_gettime(CLOCK_MONOTONIC, &curtime);
		skynet_error(nullptr, "ms-timer: poller %d on timeout timer session %d at (%ld, %ld) curtime (%ld, %ld) dt:(%ld, %ld)", poller->ID(), node->session, now->tv_sec, now->tv_nsec, curtime.tv_sec, curtime.tv_nsec, curtime.tv_sec-now->tv_sec, curtime.tv_nsec-now->tv_nsec);
#endif
		node->OnTimeout(now);
		popHeap();
		if (node->count > 0) {
			if (--node->count <= 0) {
				timer_nodes.erase(key);
				delete node;
			} else {
				get_timeout(&node->timeout, now, node->interval_ms);
				pushHeap(node);
			}
		} else {
			get_timeout(&node->timeout, now, node->interval_ms);
			pushHeap(node);
		}
	}

	if (container.size() > 0) {
		TimerNode *top = container.front();
		timespec gap;
		get_timeout_gap(&gap, now, &top->timeout);
		poller->SetTimerFd(&gap);
	}
}

void TimerPool::Release(const Poller *poller) {
	// Notice: container持有的元素可能比timer_nodes多
	for (auto iter = container.begin(); iter != container.end(); iter++) {
		skynet_error(nullptr, "ms-timer: timer pool %d release session %d", poller->ID(), (*iter)->session);
		delete *iter;
	}
	container.clear();
	timer_nodes.clear();
}

} // namespace ms_timer
} // namespace skynet_ext