#pragma once

namespace skynet_ext {
namespace ms_timer {

#ifndef DEBUG_LOG_OUTPUT
#define DEBUG_LOG_OUTPUT 0
#endif

enum ErrCode {
	OK = 0,
	POLLER_CREATE_ERROR = -1,
	PIPE_CREATE_ERROR = -2,
	POLLER_ADD_ERROR = -3,
	TIMERFD_CREATE_ERROR = -4,
	TIMERFD_SET_NONBLOCK_ERROR = -5,
	API_PARAM1_ERROR = -6,
	API_PARAM2_ERROR = -7,
	API_PARAM3_ERROR = -8,
	API_PARAM4_ERROR = -9,
	UNKNOWN_ERROR = -10
};

} // namespace ms_timer
} // namespace skynet_ext