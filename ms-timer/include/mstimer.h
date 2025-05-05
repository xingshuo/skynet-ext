#pragma once
#include <stdint.h>

namespace skynet_ext {
namespace ms_timer {
	enum ErrCode {
		OK					 = 0,
		POLLER_CREATE_ERROR	 = -1,
		PIPE_CREATE_ERROR	 = -2,
		POLLER_ADD_ERROR	 = -3,
		TIMERFD_CREATE_ERROR = -4,
		TIMERFD_SET_NONBLOCK_ERROR = -5,
	};

	int InitPoller();
	void ExitPoller();
	int StartTimer(uint32_t service_handle, int session, int count, uint32_t interval_ms);
	void StopTimer(uint32_t service_handle, int session);
} // namespace ms_timer
} // namespace skynet_ext