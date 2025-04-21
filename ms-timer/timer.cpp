#include "timer.h"

namespace skynet_ext {

TimerPool::TimerPool() {
	timer_fd = -1;
	heapq = nullptr;
	size = 0;
	capacity = 0
}

App::App() {

}

App::~App() {

}

}