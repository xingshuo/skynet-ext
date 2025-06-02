#include "signal/service_signal_mgr.h"

namespace skynet_ext {
namespace signal {

static SignalMngr *signal_mgr = nullptr;

static void signal_handler(int sig) {
	signal_mgr->OnSignalNotify(sig);
}

SignalMngr::SignalMngr() {
	skynet_error(nullptr, "signal-mgr: ctor %p", this);
	memset(ref, 0, sizeof(ref));
	ctx = nullptr;
	pipe_rd = -1;
	pipe_wr = -1;
	rd_socket_id = -1;
}

SignalMngr::~SignalMngr() {
	skynet_error(ctx, "signal-mgr: dtor %p", this);
	std::vector<uint32_t> watchers;
	watchers.clear();
	for (auto iter = handlers.begin(); iter != handlers.end(); iter++) {
		uint32_t service_handle = iter->first;
		watchers.push_back(service_handle);
	}
	for (auto iter = watchers.begin(); iter != watchers.end(); iter++) {
		UnregisterWatcher(*iter, 0);
	}
	if (rd_socket_id >= 0) {
		skynet_socket_close(ctx, rd_socket_id);
		rd_socket_id = -1;
	}
	if (pipe_rd != -1) {
		close(pipe_rd);
		pipe_rd = -1;
	}
	if (pipe_wr != -1) {
		close(pipe_wr);
		pipe_wr = -1;
	}
}

int SignalMngr::Init(skynet_context *ctx) {
	int pipe_fds[2]; // {rfd, wfd}
	if (pipe(pipe_fds)) {
		skynet_error(ctx, "signal-mgr: create pipe failed.");
		return -1;
	}
	// FIXME: pipe_wr是否需要设置成非阻塞?
	int32_t flags = fcntl(pipe_fds[1], F_GETFL, 0);
	flags < 0 ? flags = O_NONBLOCK : flags |= O_NONBLOCK;
	if (fcntl(pipe_fds[1], F_SETFL, flags) < 0) {
		skynet_error(ctx, "signal-mgr: fcntl pipe write fd(%d) noblocking error %s", pipe_fds[1], strerror(errno));
		return -2;
	}
	int id = skynet_socket_bind(ctx, pipe_fds[0]);
	if (id < 0) {
		skynet_error(ctx, "signal-mgr: skynet socket bind error fd(%d)", pipe_fds[0]);
		return -3;
	}
	skynet_error(ctx, "signal-mgr: skynet socket bind pipe readfd %d to %d", pipe_fds[0], id);

	this->ctx = ctx;
	pipe_rd = pipe_fds[0];
	pipe_wr = pipe_fds[1];
	rd_socket_id = id;
	return 0;
}

int SignalMngr::RegisterWatcher(uint32_t service_handle, int sig) {
	if (sig <= 0 || sig >= numSig) {
		skynet_error(ctx, "signal-mgr: register watcher error, service:%u sig:%d", service_handle, sig);
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
	if (!h->Get(sig)) {
		h->Set(sig);
		skynet_error(ctx, "signal-mgr: set signal mask %d, %u", sig, service_handle);
		if (ref[sig]++ == 0) {
			std::signal(sig, signal_handler);
			skynet_error(ctx, "signal-mgr: set signal handler %d, %u", sig, service_handle);
		}
	}
	skynet_error(ctx, "signal-mgr: register watcher succ, service:%u sig:%d", service_handle, sig);
	return 0;
}

void SignalMngr::UnregisterWatcher(uint32_t service_handle, int sig) {
	if (sig < 0 || sig >= numSig) {
		skynet_error(ctx, "signal-mgr: unregister watcher error, service:%u sig:%d", service_handle, sig);
		return;
	}
	auto iter = handlers.find(service_handle);
	if (iter == handlers.end()) {
		return;
	}
	SignalHandler *h = iter->second;
	if (sig == 0) { // 注销全部
		handlers.erase(service_handle);
		for (int n = 0; n < numSig; n++) {
			if (h->Get(n)) {
				skynet_error(ctx, "signal-mgr: clear signal mask %d, %u, 0", n, service_handle);
				if (--ref[n] == 0) {
					std::signal(n, SIG_DFL);
					skynet_error(ctx, "signal-mgr: set signal default %d, %u, 0", n, service_handle);
				}
			}
		}
		delete h;
		skynet_error(ctx, "signal-mgr: delete watcher %u, 0", service_handle);
	} else {
		if (h->Get(sig)) {
			h->Clear(sig);
			skynet_error(ctx, "signal-mgr: clear signal mask %d, %u", sig, service_handle);
			if (--ref[sig] == 0) {
				std::signal(sig, SIG_DFL);
				skynet_error(ctx, "signal-mgr: set signal default %d, %u", sig, service_handle);
			}
		}
		if (h->IsEmpty()) {
			handlers.erase(service_handle);
			delete h;
			skynet_error(ctx, "signal-mgr: delete watcher %u", service_handle);
		}
	}
}

void SignalMngr::OnSignalNotify(int sig) {
	if (pipe_wr != -1) {
		uint8_t n = static_cast<uint8_t>(sig);
		write(pipe_wr, &n, sizeof(n));
	}
}

void SignalMngr::DispatchToWatchers(skynet_context *ctx, int sig) {
	if (sig <= 0 || sig >= numSig) {
		skynet_error(ctx, "signal-mgr: dispatch to watchers error, sig:%d", sig);
		return;
	}
	for (auto iter = handlers.begin(); iter != handlers.end(); iter++) {
		uint32_t service_handle = iter->first;
		SignalHandler *h = iter->second;
		if (h->Get(sig)) {
			struct skynet_message message;
			message.source = skynet_context_handle(ctx);
			message.session = 0;
			message.data = NULL;
			message.sz = (size_t)sig | (size_t)PTYPE_SIGNAL << MESSAGE_TYPE_SHIFT;
			skynet_context_push(service_handle, &message);
			skynet_error(ctx, "signal-mgr: dispatch to watcher %u, sig:%d", service_handle, sig);
		}
	}
}


static int
_cb(skynet_context *ctx, void *ud, int type, int session, uint32_t source, const void *msg, size_t sz) {
	SignalMngr *mgr = static_cast<SignalMngr *>(ud);
	switch(type) {
	case PTYPE_SOCKET: {
		const skynet_socket_message *sm = static_cast<const skynet_socket_message *>(msg);
		if (sm->type == SKYNET_SOCKET_TYPE_DATA && sm->id == mgr->ReadSocketId()) {
			const char *buf = sm->buffer;
			int sz = sm->ud;
			for (int i = 0; i < sz; i++) {
				int sig = static_cast<int>(buf[i]);
				mgr->DispatchToWatchers(ctx, sig);
			}
			skynet_free(sm->buffer);
		}
		break;
	}
	case PTYPE_SIGNAL: {
		const char *command = static_cast<const char *>(msg);
		int i;
		for (i = 0; i < (int)sz; i++) {
			if (command[i]==' ') {
				break;
			}
		}
		if (std::memcmp(command,"RegisterWatcher",i) == 0) {
			int sig = strtol(command+i+1, NULL, 10);
			mgr->RegisterWatcher(source, sig);
		} else if (std::memcmp(command,"UnregisterWatcher",i) == 0) {
			int sig = strtol(command+i+1, NULL, 10);
			mgr->UnregisterWatcher(source, sig);
		} else {
			skynet_error(ctx, "signal-mgr: unknown command %s", command);
		}
		break;
	}

	};
	return 0;
}

extern "C" SignalMngr*
signal_mgr_create(void) {
	assert(signal_mgr == nullptr);
	signal_mgr = new SignalMngr;
	return signal_mgr;
}

extern "C" void
signal_mgr_release(SignalMngr *mgr) {
	if (signal_mgr != nullptr) {
		delete signal_mgr;
		signal_mgr = nullptr;
	}
}

extern "C" int
signal_mgr_init(SignalMngr *mgr, skynet_context *ctx, char *parm) {
	assert(mgr == signal_mgr);
	int ret = signal_mgr->Init(ctx);
	if (ret) {
		delete signal_mgr;
		signal_mgr = nullptr;
		return ret;
	}
	skynet_callback(ctx, signal_mgr, _cb);
	skynet_handle_namehandle(skynet_context_handle(ctx), "signal-mgr");
	return 0;
}

} // namespace signal
} // namespace skynet_ext