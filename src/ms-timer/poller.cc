#include <sys/epoll.h>
#include <sys/timerfd.h>
#include <cstring>
#include <assert.h>
#include <fcntl.h>

#include "ms-timer/common.h"
#include "ms-timer/poller.h"

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

Poller::Poller() {
	skynet_error(nullptr, "ms-timer: ctor %p", this);
	this->poll_fd_ = -1;
	this->pipe_rd = -1;
	this->pipe_wr = -1;
	this->timer_fd_ = -1;
	this->id_ = -1;
}

Poller::~Poller() {
	skynet_error(nullptr, "ms-timer: dtor %p id:%d", this, id_);
	if (id_ == -1) {
		return;
	}
	RequestMsg request;
	SendRequest(&request, 'X', 0);
	if (thread_.joinable()) {
		thread_.join();
	}
	// release timer nodes
	timer_pool.Release(this);
	// release timer fd
	epoll_ctl(poll_fd_, EPOLL_CTL_DEL, timer_fd_, nullptr);
	close(timer_fd_);
	// release pipe
	epoll_ctl(poll_fd_, EPOLL_CTL_DEL, pipe_rd, nullptr);
	close(pipe_rd);
	close(pipe_wr);
	// release epoll fd
	close(poll_fd_);
	skynet_error(nullptr, "ms-timer: release %p id:%d", this, id_);
}

int Poller::Init(int id) {
	int poll_fd;
	int timer_fd;
	int pipe_fds[2]; // {rfd, wfd}
	int ec = ErrCode::UNKNOWN_ERROR;
	int32_t flags;
	// init epoll fd
	poll_fd = epoll_create(1024);
	if (poll_fd < 0) {
		skynet_error(nullptr, "ms-timer: poller %d create event pool failed.", id);
		return ErrCode::POLLER_CREATE_ERROR;
	}
	// init pipe
	if (pipe(pipe_fds)) {
		skynet_error(nullptr, "ms-timer: poller %d create pipe pair failed.", id);
		ec = ErrCode::PIPE_CREATE_ERROR;
		goto _failed_init_pipe;
	}
	// add pipe read fd to epoll fd
	if (poller_add_fd(poll_fd, pipe_fds[0], EPOLLIN, nullptr)) {
		skynet_error(nullptr, "ms-timer: poller %d can't add pipe read fd to event pool.", id);
		ec = ErrCode::POLLER_ADD_ERROR;
		goto _failed_set_pipe;
	}
	// init timer fd
	timer_fd = timerfd_create(CLOCK_MONOTONIC, 0);
	if (timer_fd < 0) {
		skynet_error(nullptr, "ms-timer: poller %d create timerfd error %s", id, strerror(errno));
		ec = ErrCode::TIMERFD_CREATE_ERROR;
		goto _failed_init_timerfd;
	}
	// set timer fd noblocking
	// 使用fcntl替代timerfd_create的第2个参数兼容内核版本
	flags = fcntl(timer_fd, F_GETFL, 0);
	flags < 0 ? flags = O_NONBLOCK : flags |= O_NONBLOCK;
	if (fcntl(timer_fd, F_SETFL, flags) < 0) {
		skynet_error(nullptr, "ms-timer: poller %d fcntl timerfd(%d) noblocking error %s", id, timer_fd, strerror(errno));
		ec = ErrCode::TIMERFD_SET_NONBLOCK_ERROR;
		goto _failed_set_timerfd;
	}
	// add timer fd to epoll fd
	if (poller_add_fd(poll_fd, timer_fd, EPOLLIN | EPOLLET, this)) {
		skynet_error(nullptr, "ms-timer: poller %d add timerfd(%d) to event pool error %s", id, timer_fd, strerror(errno));
		ec = ErrCode::POLLER_ADD_ERROR;
		goto _failed_set_timerfd;
	}

	this->id_ = id;
	this->poll_fd_ = poll_fd;
	this->pipe_rd = pipe_fds[0];
	this->pipe_wr = pipe_fds[1];
	this->timer_fd_ = timer_fd;
	thread_ = std::thread{&Poller::poll, this};
	return ErrCode::OK;
_failed_set_timerfd:
	close(timer_fd);
_failed_init_timerfd:
	epoll_ctl(poll_fd, EPOLL_CTL_DEL, pipe_fds[0], nullptr);
_failed_set_pipe:
	close(pipe_fds[0]);
	close(pipe_fds[1]);
_failed_init_pipe:
	close(poll_fd);
	return ec;
}

void Poller::poll() {
	skynet_error(nullptr, "ms-timer: poller %d %p", id_, this);
	epoll_event events[POLLER_EVENTS_MAX];
	char buf[POLLER_BUFSIZE];
	timespec now;
	int has_pipe_event;
	int has_timer_event;

	while (1) {
		int n = epoll_wait(poll_fd_, events, POLLER_EVENTS_MAX, -1);
		has_pipe_event = 0;
		has_timer_event = 0;
		for (int i = 0; i < n; i++) {
			if (static_cast<Poller *>(events[i].data.ptr) == nullptr) {
				has_pipe_event = 1;
			} else {
				has_timer_event = 1;
			}
		}
		clock_gettime(CLOCK_MONOTONIC, &now);
		if (has_timer_event) {
			while (read(timer_fd_, buf, POLLER_BUFSIZE) > 0) {}
			timer_pool.CheckTimeout(this, &now);
		}
		if (has_pipe_event) {
			if (handlePipe(&now)) {
				break;
			}
		}
	}
	skynet_error(nullptr, "ms-timer: poller %d quit polling", id_);
}

void Poller::blockReadPipe(void *buffer, int sz) {
	while (1) {
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
		timer_pool.AddTimer(this, reinterpret_cast<RequestAddTimer *>(buffer), now);
		return 0;
	case 'D':
		timer_pool.DelTimer(this, reinterpret_cast<RequestDelTimer *>(buffer));
		return 0;
	case 'X':
		skynet_error(nullptr, "ms-timer: poller %d recv quit cmd", id_);
		return 1;
	default:
		skynet_error(nullptr, "ms-timer: poller %d unknown pipe cmd %c.", id_, type);
		return 1;
	}
	return 1;
}

void Poller::SendRequest(RequestMsg *request, char type, int len) {
	request->header[6] = (uint8_t)type;
	request->header[7] = (uint8_t)len;
	const char *req = (const char *)request + offsetof(RequestMsg, header[6]);
	while (1) {
		ssize_t n = write(pipe_wr, req, len+2);
		if (n<0) {
			if (errno != EINTR) {
				skynet_error(nullptr, "ms-timer: poller %d send ctrl command error %s.", id_, strerror(errno));
			}
			continue;
		}
		assert(n == len+2);
		return;
	}
}

int Poller::SetTimerFd(const timespec *time) {
#if DEBUG_LOG_OUTPUT
	skynet_error(nullptr, "ms-timer: poller %d timerfd %d settime (%ld, %ld)", id_, timer_fd_, time->tv_sec, time->tv_nsec);
#endif
	itimerspec timer = {
		.it_interval = { },
		.it_value =	*time
	};
	int ret = timerfd_settime(timer_fd_, 0, &timer, nullptr);
	if (ret < 0) {
		// TODO: 异常处理
		skynet_error(nullptr, "ms-timer: poller %d timerfd %d settime (%ld, %ld) failed:%s", id_, timer_fd_, time->tv_sec, time->tv_nsec, strerror(errno));
	}
	return ret;
}

} // namespace ms_timer
} // namespace skynet_ext