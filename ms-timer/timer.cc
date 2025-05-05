#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>


extern "C" {
#include "skynet_server.h"
#include "skynet_mq.h"

struct skynet_context;
void skynet_error(struct skynet_context* context, const char* msg, ...);
}

#include "timer.h"
#include "poller.h"

#define SKYNET_PTYPE_RESPONSE 1 // same as `PTYPE_RESPONSE` in skynet.h

namespace skynet_ext {
namespace ms_timer {


static void get_timeout(timespec *timeout, timespec *now, int delta_ms) {
	*timeout = *now;
	timeout->tv_sec += delta_ms / 1000;
	timeout->tv_nsec += delta_ms % 1000 * 1000000;
	if (timeout->tv_nsec >= 1000000000) {
		timeout->tv_nsec -= 1000000000;
		timeout->tv_sec++;
	}
}

static int get_timeout_gap(timespec *gap, timespec *now, timespec *timeout) {
	if (now->tv_sec > timeout->tv_sec || (now->tv_sec == timeout->tv_sec && now->tv_nsec >= timeout->tv_nsec)) {
		// touch timeout at once
		gap->tv_sec = 0;
		gap->tv_nsec = 0;
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


TimerNode::TimerNode(uint32_t service_handle, int session, int count, uint32_t interval_ms, timespec *now) {
	this->service_handle = service_handle;
	this->session = session;
	this->count = count;
	this->interval_ms = interval_ms;
	get_timeout(&this->timeout, now, interval_ms);
	this->state = TimerState::PENDING;
}

void TimerNode::OnTimeout() {
	// dispatch timeout message to register service
	skynet_message message;
	message.source = 0;
	message.session = this->session;
	message.data = NULL;
	message.sz = (size_t)SKYNET_PTYPE_RESPONSE << MESSAGE_TYPE_SHIFT;
	skynet_context_push(this->service_handle, &message);
}

TimerPool::TimerPool() {
	timer_fd = -1;
	id_ = -1;
}

TimerPool::~TimerPool() {
	// Notice: container持有的元素可能比timer_nodes多
	for (auto iter = container.begin(); iter != container.end(); iter++) {
		skynet_error(nullptr, "ms-timer: timer pool %d release session %d", id_, (*iter)->session);
		delete *iter;
	}
	container.clear();
	timer_nodes.clear();
}

int TimerPool::CheckInit(Poller *poller, int id) {
	if (timer_fd == -1) {
		int fd = poller->CreateTimerFd(this);
		if (fd < 0) {
			return 1;
		}
		timer_fd = fd;
		id_ = id;
		skynet_error(nullptr, "ms-timer: timer pool %d init timerfd %d", id, timer_fd);
	}
	return 0;
}

void TimerPool::Release(Poller *poller) {
	if (timer_fd >= 0) {
		poller->CloseTimerFd(timer_fd);
		skynet_error(nullptr, "ms-timer: timer pool %d release timerfd %d", id_, timer_fd);
		timer_fd = -1;
	}
}

void TimerPool::AddTimer(Poller *poller, RequestAddTimer *request, timespec *now) {
	TimerNode *node = new TimerNode(request->service_handle, request->session, request->count, request->interval_ms, now);
	auto iter = timer_nodes.find(node->session);
	if (iter != timer_nodes.end()) {
		TimerNode* lastnode = iter->second;
		skynet_error(nullptr, "ms-timer: timer pool %d addtimer remove last session:%d node:%p", id_, lastnode->session, lastnode);
		lastnode->state = TimerState::CANCELLED;
		lastnode->session = 0; // skynet.call invalid session id
	}
	timer_nodes[node->session] = node;
	pushHeap(node);
	skynet_error(nullptr, "ms-timer: timer pool %d addtimer session:%d node:%p", id_, node->session, node);
	TimerNode *top = container.front();
	timespec gap;
	get_timeout_gap(&gap, now, &top->timeout);
	poller->SetTimerFd(timer_fd, &gap);
}

void TimerPool::DelTimer(int session) {
	auto iter = timer_nodes.find(session);
	if (iter == timer_nodes.end()) {
		skynet_error(nullptr, "ms-timer: timer pool %d deltimer unknown session:%d", id_, session);
		return;
	}
	TimerNode* node = iter->second;
	node->state = TimerState::CANCELLED;
	skynet_error(nullptr, "ms-timer: timer pool %d deltimer session:%d", id_, session);
}

void TimerPool::CheckTimeout(Poller *poller, timespec *now) {
	const uint32_t BUFF_LEN = 128;
	char buf[BUFF_LEN] = {0};
	while (read(timer_fd, buf, BUFF_LEN) > 0) {}

	while (container.size() > 0) {
		TimerNode *node = container.front();
		if (node->state == TimerState::CANCELLED) {
			popHeap();
			timer_nodes.erase(node->session);
			delete node;
			continue;
		}
		if (timeout_cmp(&node->timeout, now) > 0) {
			break;
		}
		node->OnTimeout();
		popHeap();
		if (node->count > 0) {
			if (--node->count <= 0) {
				timer_nodes.erase(node->session);
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
		poller->SetTimerFd(timer_fd, &gap);
	}
}

} // namespace ms_timer
} // namespace skynet_ext