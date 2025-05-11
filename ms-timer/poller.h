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

#define POLLER_EVENTS_MAX 64
#define POLLER_BUFSIZE 128

class Poller {
public:
	Poller();
	~Poller();

	int Init(int id);
	int ID() const {
		return id_;
	}
	void SendRequest(RequestMsg *request, char type, int len);
	int SetTimerFd(timespec *time);

private:
	void poll();
	int handlePipe(timespec *now);
	void blockReadPipe(void *buffer, int sz);

private:
	int poll_fd_;
	int pipe_rd;
	int pipe_wr;
	int timer_fd_;
	TimerPool timer_pool;
	std::thread thread_;
	int id_;
	bool is_polling;
};

} // namespace ms_timer
} // namespace skynet_ext