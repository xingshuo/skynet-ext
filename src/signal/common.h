#pragma once

namespace skynet_ext {
namespace signal {

#ifndef PTYPE_SIGNAL
#define PTYPE_SIGNAL 17 // Notice: must be different with `PTYPE_XXX` defined in "skynet.h"
#endif

enum ErrCode {
	OK = 0,
	PIPE_CREATE_ERROR = -1,
	FD_SET_NONBLOCK_ERROR = -2,
	SKYNET_SOCKET_BIND_ERROR = -3,
	SIGNAL_NUM_ERROR = -4
};

} // namespace signal
} // namespace skynet_ext