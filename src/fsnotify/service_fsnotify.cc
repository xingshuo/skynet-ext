#include "fsnotify/service_fsnotify.h"
#include <errno.h>
#include <unistd.h>
#include <algorithm>
#include <vector>
#include <cstdlib>
#include <cassert>
#include "fsnotify/common.h"

namespace skynet_ext {
namespace fsnotify {

// NOTICE: 未包含: 被访问、打开、关闭
const uint32_t
kWatchEventsMask = IN_ATTRIB
		| IN_CREATE
		| IN_MODIFY
		| IN_DELETE
		| IN_DELETE_SELF
		| IN_MOVE_SELF
		| IN_MOVED_FROM
		| IN_MOVED_TO;

std::vector<std::string> StringSplit(std::string_view str, char sep, int count) {
	std::vector<std::string> res;
	if (count == 0) {
		res.emplace_back(str);
		return res;
	}
	std::string_view::const_iterator cur = str.begin();
	std::string_view::const_iterator end = str.end();
	std::string_view::const_iterator next = std::find(cur, end, sep);

	while (next != end) {
		res.emplace_back(cur, next);
		cur = next + 1;
		if (count > 0 && --count == 0) {
			next = end;
			break;
		}
		next = std::find(cur, end, sep);
	}

	res.emplace_back(cur, next);
	return res;
}

FSNotifyManager::FSNotifyManager() {
	ctx_ = nullptr;
	inotify_fd_ = -1;
	inotify_sid_ = -1;
}

FSNotifyManager::~FSNotifyManager() {
	if (inotify_sid_ >= 0) {
		// NOTICE:
		//  1. 接口不会close通过skynet_socket_bind关联的fd
		//  2. 存在异步风险: 可能fd先被后续流程close, 再被EPOLL_CTL_DEL
		skynet_socket_close(ctx_, inotify_sid_);
		inotify_sid_ = -1;
	}

	if (inotify_fd_ >= 0) {
		// 移除监听wd
		for (const auto& iter : wd_paths_) {
			inotify_rm_watch(inotify_fd_, iter.first);
		}
		close(inotify_fd_);
		inotify_fd_ = -1;
	}
}

int FSNotifyManager::Init(skynet_context *ctx) {
	int fd = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
	if (fd < 0) {
		skynet_error(ctx, "fsnotify: init failed %s", strerror(errno));
		return ErrCode::INOTIFY_INIT_ERROR;
	}
	// NOTICE: 该接口指定框架层socket->protocol字段为PROTOCOL_TCP类型
	int sid = skynet_socket_bind(ctx, fd);
	if (sid < 0) {
		close(fd);
		skynet_error(ctx, "fsnotify: inotify-fd socket bind failed");
		return ErrCode::SKYNET_SOCKET_BIND_ERROR;
	}

	ctx_ = ctx;
	inotify_fd_ = fd;
	inotify_sid_ = sid;
	skynet_error(ctx, "fsnotify: init succ inotify-fd: %d socket-id: %d", inotify_fd_, inotify_sid_);

	return ErrCode::OK;
}

void FSNotifyManager::addWatchPath(uint32_t service_handle, const std::string& watch_path, uint32_t watch_events) {
	if (watch_events == 0 || (watch_events & ~kWatchEventsMask)) {
		skynet_error(ctx_, "fsnotify: addWatchPath unknown events, service: %x path: %s events: %u", service_handle, watch_path.c_str(), watch_events);
		return;
	}
	if (watchers_.find(watch_path) == watchers_.end()) {
		int wd = inotify_add_watch(inotify_fd_, watch_path.c_str(), kWatchEventsMask);
		if (wd == -1) {
			skynet_error(ctx_, "fsnotify: inotify_add_watch failed, path: %s err: %s", watch_path.c_str(), strerror(errno));
			return;
		}
		watchers_[watch_path].wd_ = wd;
		wd_paths_[wd] = watch_path;
		skynet_error(ctx_, "fsnotify: inotify_add_watch succ, path: %s wd: %d", watch_path.c_str(), wd);
	}
	watchers_[watch_path].services_[service_handle] = watch_events;
	skynet_error(ctx_, "fsnotify: addWatchPath succ, service: %x path: %s events: %u", service_handle, watch_path.c_str(), watch_events);
}

void FSNotifyManager::rmWatchPath(uint32_t service_handle, const std::string& watch_path) {
	auto iter = watchers_.find(watch_path);
	if (iter == watchers_.end()) {
		skynet_error(ctx_, "fsnotify: rmWatchPath non-existent, service: %x path: %s", service_handle, watch_path.c_str());
		return;
	}
	auto& entry = iter->second;
	int wd = entry.wd_;
	int result = entry.services_.erase(service_handle);
	skynet_error(ctx_, "fsnotify: rmWatchPath result: %d size: %zu, service: %x path: %s wd: %d", result, entry.services_.size(), service_handle, watch_path.c_str(), wd);
	if (!entry.services_.empty()) {
		return;
	}
	watchers_.erase(iter);
	wd_paths_.erase(wd);
	inotify_rm_watch(inotify_fd_, wd);
	skynet_error(ctx_, "fsnotify: inotify_rm_watch succ, path: %s wd: %d", watch_path.c_str(), wd);
}

void FSNotifyManager::rmWatchService(uint32_t service_handle) {
	std::vector<std::string> path_list;
	for (const auto& iter : watchers_) {
		const std::string& path = iter.first;
		const auto& service_map = iter.second.services_;
		if (service_map.find(service_handle) != service_map.end()) {
			path_list.push_back(path);
		}
	}

	for (const auto& path : path_list) {
		rmWatchPath(service_handle, path);
	}
}

void FSNotifyManager::dispatchInotifyEvent(const inotify_event *ev, size_t data_sz) {
	if (ev->mask & IN_Q_OVERFLOW) { // ev->wd == -1
		skynet_error(ctx_, "fsnotify: inotify queue overflow, events may be lost");
		return;
	}
	auto path_iter = wd_paths_.find(ev->wd);
	if (path_iter == wd_paths_.end()) {
		return;
	}

	const std::string watch_path = path_iter->second;
	const size_t wpath_sz = watch_path.size();
	const bool is_ignored = (ev->mask & IN_IGNORED);
	if (is_ignored) {
		wd_paths_.erase(path_iter);
	}

	auto entry_iter = watchers_.find(watch_path);
	if (entry_iter == watchers_.end()) {
		return;
	}
	const auto& service_map = entry_iter->second.services_;

	auto push_to = [&](uint32_t service_handle, const void *ev_data, size_t ev_sz) {
		size_t total_sz = ev_sz + wpath_sz;
		void *payload = skynet_malloc(total_sz);
		memcpy(payload, ev_data, ev_sz);
		memcpy((char *)payload + ev_sz, watch_path.c_str(), wpath_sz);
		skynet_message msg;
		msg.source = skynet_context_handle(ctx_);
		msg.session = 0;
		msg.data = payload;
		msg.sz = total_sz | (size_t)PTYPE_FSNOTIFY << MESSAGE_TYPE_SHIFT;
		if (skynet_context_push(service_handle, &msg)) {
			// NOTICE: 不清理 service 数据结构, 依赖 service 退出时主动调 RmWatchService
			skynet_free(payload);
		}
	};

	if (is_ignored) {
		inotify_event synthetic{};
		synthetic.wd = ev->wd;
		synthetic.mask = IN_IGNORED;
		for (const auto& iter : service_map) {
			push_to(iter.first, &synthetic, sizeof(inotify_event));
		}
		watchers_.erase(entry_iter);
	} else {
		for (const auto& iter : service_map) {
			if (ev->mask & iter.second) {
				push_to(iter.first, ev, data_sz);
			}
		}
	}
}

void FSNotifyManager::DispatchSocketMessage(const skynet_socket_message* message) {
	if (message->type != SKYNET_SOCKET_TYPE_DATA) {
		return;
	}
	if (message->id != inotify_sid_) {
		skynet_free(message->buffer);
		skynet_error(ctx_, "fsnotify: unknown socket-id: %d", message->id);
		return;
	}

	const char* buf = message->buffer;
	int sz = message->ud;
	recv_buffer_.append(buf, sz);
	size_t offset = 0;
	while (offset + sizeof(inotify_event) <= recv_buffer_.size()) {
		const auto* ev = reinterpret_cast<const inotify_event*>(recv_buffer_.data() + offset);
		size_t data_sz = sizeof(inotify_event) + ev->len;
		if (offset + data_sz > recv_buffer_.size()) {
			break;
		}
		dispatchInotifyEvent(ev, data_sz);
		offset += data_sz;
	}
	if (offset > 0) {
		recv_buffer_.erase(0, offset);
	}
	skynet_free(message->buffer);
}

void FSNotifyManager::DispatchCommand(uint32_t source, const char* msg, int sz) {
	// NOTICE: skynet 不含 PTYPE_TAG_DONTCOPY 的消息保证 msg[sz] == '\0'
	auto args = StringSplit(std::string_view(msg, sz), ' ', 2);
	if (args.empty()) {
		skynet_error(ctx_, "fsnotify: command args empty");
		return;
	}

	const std::string& cmd = args[0];
	if (cmd == "AddWatchPath") {
		if (args.size() != 3) {
			skynet_error(ctx_, "fsnotify: addWatchPath bad args");
			return;
		}
		errno = 0;
		char* end_ptr = nullptr;
		unsigned long events_mask = std::strtoul(args[2].c_str(), &end_ptr, 10);
		if (end_ptr == args[2].c_str() || *end_ptr != '\0' || errno == ERANGE || events_mask > UINT32_MAX) {
			skynet_error(ctx_, "fsnotify: addWatchPath bad mask: %s", args[2].c_str());
			return;
		}
		addWatchPath(source, args[1], static_cast<uint32_t>(events_mask));
		return;
	}
	if (cmd == "RmWatchPath") {
		if (args.size() != 2) {
			skynet_error(ctx_, "fsnotify: rmWatchPath missing watch-path");
			return;
		}
		rmWatchPath(source, args[1]);
		return;
	}
	if (cmd == "RmWatchService") {
		rmWatchService(source);
		return;
	}
	skynet_error(ctx_, "fsnotify: unknown command from %x, size: %d", source, sz);
}

static FSNotifyManager *fsnotify_mgr = nullptr; 

static int
_cb(skynet_context *ctx, void *ud, int type, int session, uint32_t source, const void *msg, size_t sz) {
	(void)ctx;
	(void)session;
	FSNotifyManager *mgr = static_cast<FSNotifyManager *>(ud);
	switch (type) {
	case PTYPE_SOCKET:
		mgr->DispatchSocketMessage(static_cast<const skynet_socket_message *>(msg));
		break;
	case PTYPE_FSNOTIFY:
		mgr->DispatchCommand(source, static_cast<const char *>(msg), sz);
		break;
	}

	return 0;
}

extern "C" FSNotifyManager*
fsnotify_create(void) {
	assert(fsnotify_mgr == nullptr);
	fsnotify_mgr = new FSNotifyManager;
	return fsnotify_mgr;
}

extern "C" void
fsnotify_release(FSNotifyManager *mgr) {
	if (fsnotify_mgr == mgr) {
		delete fsnotify_mgr;
		fsnotify_mgr = nullptr;
	}
}

extern "C" int
fsnotify_init(FSNotifyManager *mgr, skynet_context *ctx, char *parm) {
	(void)parm;
	assert(mgr == fsnotify_mgr);
	int ret = fsnotify_mgr->Init(ctx);
	if (ret) {
		delete fsnotify_mgr;
		fsnotify_mgr = nullptr;
		return ret;
	}
	skynet_callback(ctx, fsnotify_mgr, _cb);
	skynet_handle_namehandle(skynet_context_handle(ctx), "fsnotify");
	return 0;
}

} // namespace fsnotify
} // namespace skynet_ext