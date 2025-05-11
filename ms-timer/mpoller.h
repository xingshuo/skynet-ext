#pragma once
#include "poller.h"

namespace skynet_ext {
namespace ms_timer {

struct MPoller {
	MPoller();
	~MPoller();
	int Init(uint32_t nthreads);

	Poller *poller;
	uint32_t nthreads_;
};

} // namespace ms_timer
} // namespace skynet_ext