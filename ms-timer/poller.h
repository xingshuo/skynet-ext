#pragma once
#include <stdint.h>
#include <stddef.h>
#include <unistd.h>
#include <thread>
#include "timer.h"

namespace skynet_ext {
namespace ms_timer {

struct RequestAddTimer {
	uint32_t service_handle;
	int session;
	int count; // > 0 有限次
	uint32_t interval_ms;
};

struct RequestDelTimer {
	uint32_t service_handle;
	int session;
};

/*
	'A': AddTimer
	'D': DelTimer
	'X': ExitPoll
*/
struct RequestMsg {
	uint8_t header[8];	// 6 bytes dummy
	union {
		RequestAddTimer add;
		RequestDelTimer del;
	} u;
};

#define TIMER_FD_NUM 64
#define POLLER_EVENTS_MAX 64

class Poller {
public:
	Poller(int poll_fd, int pipe_fds[2]);
	~Poller();

	void SendRequest(RequestMsg *request, char type, int len);
	int CreateTimerFd(TimerPool *pool);
	int SetTimerFd(int timer_fd, timespec *time);
	void CloseTimerFd(int timer_fd);

private:
	void poll();
	int handlePipe(timespec *now);
	void blockReadPipe(void *buffer, int sz);
	void addTimer(RequestAddTimer *request, timespec *now);
	void delTimer(RequestDelTimer *request, timespec *now);

private:
	int poll_fd;
	int pipe_rd;
	int pipe_wr;
	TimerPool timer_pool[TIMER_FD_NUM];
	std::thread thread_;
};

} // namespace ms_timer
} // namespace skynet_ext