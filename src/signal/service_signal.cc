#include "service_signal.h"

namespace skynet_ext {
namespace signal {

SignalHandler SignalMngr::zeroHandler;

int SignalMngr::RegisterWatcher(uint32_t service_handle, int signal) {
	if (signal <= 0 || signal >= numSig) {
		return -1;
	}
	SignalHandler *h = nullptr;
	auto iter = handlers.find(service_handle);
	if (iter == handlers.end()) {
		h = new SignalHandler;
		handlers[service_handle] = h;
	} else {
		h = iter->second;
	}
	if (!h->Want(signal)) {
		h->Set(signal);
		if (ref[signal]++ == 0) {
			// TODO: 注册信号handler
		}
	}
}

void SignalMngr::UnregisterWatcher(uint32_t service_handle, int signal) {
	if (signal < 0 || signal >= numSig) {
		return;
	}
	auto iter = handlers.find(service_handle);
	if (iter == handlers.end()) {
		return;
	}
	SignalHandler *h = iter->second;
	if (signal == 0) { // 注销全部
		handlers.erase(service_handle);
		for (int i = 0; i < numSig; i++) {
			if (h->Want(i)) {
				if (--ref[signal] == 0) {
					// TODO: 注销信号handler
				}
			}
		}
		delete h;
	} else {
		if (h->Want(signal)) {
			h->Clear(signal);
			if (--ref[signal] == 0) {
				// TODO: 注销信号handler
			}
		}
		if (h->Zero()) {
			handlers.erase(service_handle);
			delete h;
		}
	}
}

} // namespace signal
} // namespace skynet_ext