#pragma once
#include <cstdint>
#include <cstring>
#include <stdlib.h>
#include <errno.h>
#include <unordered_map>
#include <vector>
#include <csignal>
#include <fcntl.h>
#include <assert.h>
#include <unistd.h>

extern "C" {
#include "skynet_socket.h"
#include "skynet_server.h"
#include "skynet.h"
#include "skynet_handle.h"
#include "skynet_mq.h"
}

#ifndef PTYPE_SIGNAL
#define PTYPE_SIGNAL 17 // Notice: must be different with `PTYPE_XXX` defined in "skynet.h"
#endif

namespace skynet_ext {
namespace signal {

// Reference: https://www.chromium.org/chromium-os/developer-library/reference/linux-constants/signals/
static const int kSigMin = 1;
static const int kSigMax = 64;

class SignalHandler {
public:
	SignalHandler() {
		mask = 0;
	}

	bool Get(int sig) const {
		return (mask >> (sig-1)) & 1;
	}

	void Set(int sig) {
		mask |= (((uint64_t)1) << (sig-1));
	}

	void Clear(int sig) {
		mask &= ~(((uint64_t)1) << (sig-1));
	}

	bool IsEmpty() const {
		return mask == 0;
	}

private:
	uint64_t mask;
};

class SignalMngr {
public:
	SignalMngr();
	~SignalMngr();
	int Init(skynet_context *ctx);
	int RegisterWatcher(uint32_t service_handle, int sig);
	void UnregisterWatcher(uint32_t service_handle, int sig);
	void OnSignalNotify(int sig);
	void DispatchToWatchers(skynet_context *ctx, int sig);
	int ReadSocketId() const {
		return read_socket_id;
	}

private:
	std::unordered_map<uint32_t, SignalHandler*> handlers;
	int64_t ref[kSigMax + 1];
	skynet_context *ctx;
	int pipe_rd;
	int pipe_wr;
	int read_socket_id;
};

} // namespace signal
} // namespace skynet_ext