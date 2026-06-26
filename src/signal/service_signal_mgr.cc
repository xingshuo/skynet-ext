#include "signal/service_signal_mgr.h"
#include "signal/common.h"
#include <cinttypes>

namespace skynet_ext {
namespace signal {

static SignalMngr *signal_mgr = nullptr;

static void signal_handler(int sig) {
	signal_mgr->OnSignalNotify(sig);
}

// 用 sigaction 安装/复位信号处理,行为不依赖 feature-test 宏,在 CentOS7 与 Debian12 上一致:
// - SA_RESTART:被信号打断的慢系统调用自动重启,避免 EINTR
// - sigfillset(sa_mask):处理期间屏蔽其他信号,处理函数不被重入
// 这规避了 signal() 在 -std=c++17(strict)下可能退化为 System V 语义(handler 触发一次后被复位)的风险
static void install_handler(int sig, void (*handler)(int)) {
	struct sigaction sa;
	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = handler;
	sigfillset(&sa.sa_mask);
	sa.sa_flags = (handler == SIG_DFL) ? 0 : SA_RESTART;
	sigaction(sig, &sa, nullptr);
}

SignalMngr::SignalMngr() {
	skynet_error(nullptr, "signal-mgr: ctor %p", this);
	memset(ref, 0, sizeof(ref));
	ctx = nullptr;
	pipe_rd = -1;
	pipe_wr = -1;
	read_socket_id = -1;
}

SignalMngr::~SignalMngr() {
	skynet_error(ctx, "signal-mgr: dtor %p", this);
	std::vector<uint32_t> watchers;
	for (auto iter = handlers.begin(); iter != handlers.end(); iter++) {
		uint32_t service_handle = iter->first;
		watchers.push_back(service_handle);
	}
	for (auto iter = watchers.begin(); iter != watchers.end(); iter++) {
		UnregisterWatcher(*iter, 0);
	}
	if (read_socket_id >= 0) {
		skynet_socket_close(ctx, read_socket_id);
		read_socket_id = -1;
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
		return ErrCode::PIPE_CREATE_ERROR;
	}
	// FIXME: pipe_wr是否需要设置成非阻塞?
	int32_t flags = fcntl(pipe_fds[1], F_GETFL, 0);
	flags < 0 ? flags = O_NONBLOCK : flags |= O_NONBLOCK;
	if (fcntl(pipe_fds[1], F_SETFL, flags) < 0) {
		skynet_error(ctx, "signal-mgr: fcntl pipe write fd(%d) noblocking error %s", pipe_fds[1], strerror(errno));
		close(pipe_fds[0]);
		close(pipe_fds[1]);
		return ErrCode::FD_SET_NONBLOCK_ERROR;
	}
	int id = skynet_socket_bind(ctx, pipe_fds[0]);
	if (id < 0) {
		skynet_error(ctx, "signal-mgr: skynet socket bind error fd(%d)", pipe_fds[0]);
		close(pipe_fds[0]);
		close(pipe_fds[1]);
		return ErrCode::SKYNET_SOCKET_BIND_ERROR;
	}
	skynet_error(ctx, "signal-mgr: skynet socket bind pipe readfd %d to %d", pipe_fds[0], id);

	this->ctx = ctx;
	pipe_rd = pipe_fds[0];
	pipe_wr = pipe_fds[1];
	read_socket_id = id;
	return ErrCode::OK;
}

int SignalMngr::RegisterWatcher(uint32_t service_handle, int sig) {
	if (sig < kSigMin || sig > kSigMax) {
		skynet_error(ctx, "signal-mgr: register watcher error, [:%08x] sig:%d", service_handle, sig);
		return ErrCode::SIGNAL_NUM_ERROR;
	}
	SignalHandler &h = handlers[service_handle]; // 不存在则默认构造(mask_ = 0)
	if (!h.Get(sig)) {
		h.Set(sig);
		skynet_error(ctx, "signal-mgr: set signal mask %d, [:%08x]", sig, service_handle);
		if (ref[sig]++ == 0) {
			install_handler(sig, signal_handler);
			skynet_error(ctx, "signal-mgr: set signal handler %d, [:%08x]", sig, service_handle);
		}
	}
	skynet_error(ctx, "signal-mgr: register watcher succ, [:%08x] sig:%d", service_handle, sig);
	return ErrCode::OK;
}

void SignalMngr::UnregisterWatcher(uint32_t service_handle, int sig) {
	auto iter = handlers.find(service_handle);
	if (iter == handlers.end()) {
		return;
	}
	SignalHandler &h = iter->second;
	if (sig == 0) { // 注销全部
		// 注意:先复位信号引用计数,再 erase(erase 会使引用 h 失效)
		for (int n = kSigMin; n <= kSigMax; n++) {
			if (h.Get(n)) {
				skynet_error(ctx, "signal-mgr: clear signal mask %d, [:%08x], 0", n, service_handle);
				if (--ref[n] == 0) {
					install_handler(n, SIG_DFL);
					skynet_error(ctx, "signal-mgr: set signal default %d, [:%08x], 0", n, service_handle);
				}
			}
		}
		handlers.erase(iter);
		skynet_error(ctx, "signal-mgr: delete watcher [:%08x], 0", service_handle);
	} else {
		if (sig < kSigMin || sig > kSigMax) {
			skynet_error(ctx, "signal-mgr: unregister watcher error, [:%08x] sig:%d", service_handle, sig);
			return;
		}
		if (h.Get(sig)) {
			h.Clear(sig);
			skynet_error(ctx, "signal-mgr: clear signal mask %d, [:%08x]", sig, service_handle);
			if (--ref[sig] == 0) {
				install_handler(sig, SIG_DFL);
				skynet_error(ctx, "signal-mgr: set signal default %d, [:%08x]", sig, service_handle);
			}
		}
		if (h.IsEmpty()) {
			handlers.erase(iter);
			skynet_error(ctx, "signal-mgr: delete watcher [:%08x]", service_handle);
		}
	}
}

void SignalMngr::OnSignalNotify(int sig) {
	// 信号处理函数会异步打断任意线程,需保存/恢复 errno,避免污染被打断代码的 errno
	int saved_errno = errno;
	if (pipe_wr != -1) {
		uint8_t n = static_cast<uint8_t>(sig);
		(void)write(pipe_wr, &n, sizeof(n)); // 管道满/被打断时静默丢弃,高频信号可能合并
	}
	errno = saved_errno;
}

void SignalMngr::DispatchToWatchers(skynet_context *ctx, int sig) {
	if (sig < kSigMin || sig > kSigMax) {
		skynet_error(ctx, "signal-mgr: dispatch to watchers error, sig:%d", sig);
		return;
	}
	for (auto iter = handlers.begin(); iter != handlers.end(); iter++) {
		uint32_t service_handle = iter->first;
		SignalHandler &h = iter->second;
		if (h.Get(sig)) {
			struct skynet_message message;
			message.source = skynet_context_handle(ctx);
			message.session = 0;
			message.data = NULL;
			message.sz = (size_t)sig | (size_t)PTYPE_SIGNAL << MESSAGE_TYPE_SHIFT;
			skynet_context_push(service_handle, &message);
			skynet_error(ctx, "signal-mgr: dispatch to watcher [:%08x], sig:%d", service_handle, sig);
		}
	}
}

void SignalMngr::DebugInfo() {
	skynet_error(ctx, "signal-mgr: -----debug info start-----");
	for (auto iter = handlers.begin(); iter != handlers.end(); iter++) {
		uint32_t service_handle = iter->first;
		SignalHandler &h = iter->second;
		skynet_error(ctx, "signal-mgr: [:%08x] sigmask :%016" PRIx64, service_handle, h.Mask());
	}
	skynet_error(ctx, "signal-mgr: -----debug info end-----");
}


static int
_cb(skynet_context *ctx, void *ud, int type, int session, uint32_t source, const void *msg, size_t sz) {
	(void)session;
	SignalMngr *mgr = static_cast<SignalMngr *>(ud);
	switch(type) {
	case PTYPE_SOCKET: {
		const skynet_socket_message *sm = static_cast<const skynet_socket_message *>(msg);
		if (sm->type == SKYNET_SOCKET_TYPE_DATA && sm->id == mgr->ReadSocketId()) {
			const unsigned char *buf = reinterpret_cast<const unsigned char *>(sm->buffer);
			int len = sm->ud;
			for (int i = 0; i < len; i++) {
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
			if (command[i] == ' ') {
				break;
			}
		}
		// 注意:必须比较 token 长度,否则 memcmp 只比较前 i 字节会把前缀误判为命中
		auto match = [&](const char *kw) {
			return (size_t)i == std::strlen(kw) && std::memcmp(command, kw, i) == 0;
		};
		if (match("RegisterWatcher")) {
			int sig = strtol(command+i+1, NULL, 10);
			mgr->RegisterWatcher(source, sig);
		} else if (match("UnregisterWatcher")) {
			int sig = strtol(command+i+1, NULL, 10);
			mgr->UnregisterWatcher(source, sig);
		} else if (match("DebugInfo")) {
			mgr->DebugInfo();
		} else {
			skynet_error(ctx, "signal-mgr: unknown command %s", command);
		}
		break;
	}
	}
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
	if (signal_mgr == mgr) {
		delete signal_mgr;
		signal_mgr = nullptr;
	}
}

extern "C" int
signal_mgr_init(SignalMngr *mgr, skynet_context *ctx, char *parm) {
	(void)parm;
	assert(mgr == signal_mgr);
	if (signal_mgr->Init(ctx) != ErrCode::OK) {
		delete signal_mgr;
		signal_mgr = nullptr;
		return -1;
	}
	skynet_callback(ctx, signal_mgr, _cb);
	skynet_handle_namehandle(skynet_context_handle(ctx), "signal-mgr");
	return 0;
}

} // namespace signal
} // namespace skynet_ext