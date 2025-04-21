#pragma once

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <string>
#include <map>
#include <unordered_map>
#include <fcntl.h>
#include <unistd.h>
#include <sys/time.h>
#include <execinfo.h>
#include <time.h>
#include <assert.h>

extern "C" {
#include "skynet.h"
#include "skynet_server.h"
}

#define TIMER_FD_NUM 64

namespace skynet_ext {

class TimerNode {
	uint32_t service_handle;
	int session;
	int count;
	uint32_t interval;
	struct timespec deadline;
}

class TimerPool {
	TimerNode *heapq;
	size_t size;
	size_t capacity;
	int timer_fd;
}

class App {
public:
	App();
	~App();

public:
	int epoll_fd;
	TimerPool timer_pool[TIMER_FD_NUM];
};

}