#pragma once
#include <cstdint>
#include <cstring>
#include <unordered_map>
#include <signal.h>

namespace skynet_ext {
namespace signal {

static const int numSig = 65;

struct SignalHandler {
	SignalHandler() {
		memset(mask, 0, sizeof(mask));
	}

	bool Want(int sig) const {
		return (mask[sig / 32] >> (sig & 31)) & 1;
	}

	void Set(int sig) {
		mask[sig / 32] |= 1u << (sig & 31);
	}

	void Clear(int sig) {
		mask[sig / 32] &= ~(1u << (sig & 31));
	}

	bool Zero() const {
		return memcmp(mask, SignalMngr::zeroHandler.mask, sizeof(mask)) == 0;
	}

	uint32_t mask[(numSig + 31) / 32];
};

class SignalMngr {
public:
	SignalMngr() {
		memset(ref, 0, sizeof(ref));
	}
	int RegisterWatcher(uint32_t service_handle, int signal);
	void UnregisterWatcher(uint32_t service_handle, int signal);

	static SignalHandler zeroHandler;
private:
	std::unordered_map<uint32_t, SignalHandler*> handlers;
	int64_t ref[numSig];
};

} // namespace signal
} // namespace skynet_ext