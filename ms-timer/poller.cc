#include <sys/epoll.h>
#include <sys/timerfd.h>
#include <cstring>
#include <assert.h>
#include <fcntl.h>
#include "poller.h"
#include "include/mstimer.h"

extern "C" {
struct skynet_context;
void skynet_error(struct skynet_context* context, const char* msg, ...);
}

namespace skynet_ext {
namespace ms_timer {

static inline int poller_add_fd(int pfd, int fd, uint32_t event, void *data)
{
	epoll_event ev = {
		.events = event,
		.data = {
			.ptr = data
		}
	};
	if (epoll_ctl(pfd, EPOLL_CTL_ADD, fd, &ev) == -1) {
		return 1;
	}
	return 0;
}

Poller::Poller(int poll_fd, int pipe_fds[2]) {
	skynet_error(nullptr, "ms-timer: ctor %p", this);
	this->poll_fd = poll_fd;
	pipe_rd = pipe_fds[0];
	pipe_wr = pipe_fds[1];
	thread_ = std::thread{&Poller::poll, this};
}

Poller::~Poller() {
	skynet_error(nullptr, "ms-timer: dtor %p", this);
	RequestMsg request;
	SendRequest(&request, 'X', 0);
	if (thread_.joinable()) {
		thread_.join();
	}
	for (int i = 0; i < TIMER_FD_NUM; i++) {
		timer_pool[i].Release(this);
	}
	close(poll_fd);
	skynet_error(nullptr, "ms-timer: quit %p", this);
}

void Poller::poll() {
	skynet_error(nullptr, "ms-timer: start poll");
	epoll_event events[POLLER_EVENTS_MAX];
	timespec now;
	while (1) {
		int n = epoll_wait(poll_fd, events, POLLER_EVENTS_MAX, -1);
		clock_gettime(CLOCK_MONOTONIC, &now);
		for (int i = 0; i < n; i++) {
			TimerPool *tp = (TimerPool *)events[i].data.ptr;
			if (tp == nullptr) { // pipe read
				if (handlePipe(&now)) {
					skynet_error(nullptr, "ms-timer: quit poll");
					return;
				}
			} else { // timer fd
				tp->CheckTimeout(this, &now);
			}
		}
	}
}

void Poller::blockReadPipe(void *buffer, int sz) {
	for (;;) {
		int n = read(pipe_rd, buffer, sz);
		if (n < 0) {
			if (errno != EINTR) {
				skynet_error(nullptr, "ms-timer: read pipe error %s.", strerror(errno));
			}
			continue;
		}
		assert(n == sz);
		return;
	}
}

int Poller::handlePipe(timespec *now) {
	uint8_t buffer[256];
	uint8_t header[2];
	blockReadPipe(header, sizeof(header));
	int type = header[0];
	int len = header[1];
	blockReadPipe(buffer, len);
	switch (type) {
	case 'A':
		addTimer((RequestAddTimer *)buffer, now);
		return 0;
	case 'D':
		delTimer((RequestDelTimer *)buffer, now);
		return 0;
	case 'X':
		skynet_error(nullptr, "ms-timer: recv quit cmd");
		return 1;
	default:
		skynet_error(nullptr, "ms-timer: unknown pipe cmd %c.",type);
		return 1;
	}
	return 1;
}

void Poller::addTimer(RequestAddTimer *request, timespec *now) {
	int slot = request->service_handle % TIMER_FD_NUM;
	TimerPool *pool = &timer_pool[slot];
	if (pool->CheckInit(this, slot)) {
		// TODO: 通知source service失败
		return;
	}
	pool->AddTimer(this, request, now);
}

inline void Poller::delTimer(RequestDelTimer *request, timespec *now) {
	int slot = request->service_handle % TIMER_FD_NUM;
	timer_pool[slot].DelTimer(request->session);
}

void Poller::SendRequest(RequestMsg *request, char type, int len) {
	request->header[6] = (uint8_t)type;
	request->header[7] = (uint8_t)len;
	const char *req = (const char *)request + offsetof(RequestMsg, header[6]);
	for (;;) {
		ssize_t n = write(pipe_wr, req, len+2);
		if (n<0) {
			if (errno != EINTR) {
				skynet_error(nullptr, "ms-timer: send ctrl command error %s.", strerror(errno));
			}
			continue;
		}
		assert(n == len+2);
		return;
	}
}

int Poller::CreateTimerFd(TimerPool *pool) {
	int fd = timerfd_create(CLOCK_MONOTONIC, 0);
	if (fd < 0) {
		skynet_error(nullptr, "ms-timer: create timerfd error %s", strerror(errno));
		return ErrCode::TIMERFD_CREATE_ERROR;
	}
	int32_t flags = fcntl(fd, F_GETFL, 0);
	flags < 0 ? flags = O_NONBLOCK : flags |= O_NONBLOCK;
	if (fcntl(fd, F_SETFL, flags) < 0) {
		close(fd);
		skynet_error(nullptr, "ms-timer: fcntl timerfd(%d) noblocking error %s", fd, strerror(errno));
		return ErrCode::TIMERFD_SET_NONBLOCK_ERROR;
	}
	if (poller_add_fd(poll_fd, fd, EPOLLIN | EPOLLET, pool)) {
		close(fd);
		skynet_error(nullptr, "ms-timer: add timerfd(%d) to event pool error %s", fd, strerror(errno));
		return ErrCode::POLLER_ADD_ERROR;
	}

	return fd;
}

inline int Poller::SetTimerFd(int timer_fd, timespec *time) {
	itimerspec timer = {
		.it_interval = { },
		.it_value =	*time
	};
	int ret = timerfd_settime(timer_fd, 0, &timer, nullptr);
	if (ret < 0) {
		// TODO: 异常处理
		skynet_error(nullptr, "ms-timer: timerfd %d settime (%ld,%ld) failed:%s", timer_fd, time->tv_sec, time->tv_nsec, strerror(errno));
	}
	return ret;
}

inline void Poller::CloseTimerFd(int timer_fd) {
	epoll_ctl(poll_fd, EPOLL_CTL_DEL, timer_fd, nullptr);
	close(timer_fd);
}

static Poller *g_Poller = nullptr;

int InitPoller() {
	assert(g_Poller == nullptr);
	int poll_fd = epoll_create(1024);
	if (poll_fd < 0) {
		skynet_error(nullptr, "ms-timer: create event pool failed.");
		return ErrCode::POLLER_CREATE_ERROR;
	}
	int pipe_fds[2];
	if (pipe(pipe_fds)) {
		close(poll_fd);
		skynet_error(nullptr, "ms-timer: create pipe pair failed.");
		return ErrCode::PIPE_CREATE_ERROR;
	}
	if (poller_add_fd(poll_fd, pipe_fds[0], EPOLLIN, nullptr)) {
		skynet_error(nullptr, "ms-timer: can't add pipe read fd to event pool.");
		close(pipe_fds[0]);
		close(pipe_fds[1]);
		close(poll_fd);
		return ErrCode::POLLER_ADD_ERROR;
	}

	g_Poller = new Poller(poll_fd, pipe_fds);
	assert(g_Poller != nullptr);
	return ErrCode::OK;
}

void ExitPoller() {
	if (g_Poller != nullptr) {
		delete g_Poller;
		g_Poller = nullptr;
	}
}

int StartTimer(uint32_t service_handle, int session, int count, uint32_t interval_ms) {
	// TODO: 参数检查
	RequestMsg request;
	request.u.add = {
		.service_handle = service_handle,
		.session = session,
		.count = count,
		.interval_ms = interval_ms
	};
	g_Poller->SendRequest(&request, 'A', sizeof(request.u.add));
	return 0;
}

void StopTimer(uint32_t service_handle, int session) {
	RequestMsg request;
	request.u.del = {
		.service_handle = service_handle,
		.session = session
	};
	g_Poller->SendRequest(&request, 'D', sizeof(request.u.del));
}

} // namespace ms_timer
} // namespace skynet_ext