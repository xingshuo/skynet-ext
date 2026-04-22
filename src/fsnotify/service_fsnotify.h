#pragma once
#include <sys/inotify.h>
#include <stdint.h>
#include <cstring>
#include <string>
#include <unordered_map>

extern "C" {
#include "skynet_socket.h"
#include "skynet_server.h"
#include "skynet.h"
#include "skynet_handle.h"
#include "skynet_mq.h"
}

namespace skynet_ext {
namespace fsnotify {

class FSNotifyManager {
public:
	FSNotifyManager();
	~FSNotifyManager();
	int Init(skynet_context *ctx);
	void DispatchSocketMessage(const skynet_socket_message* message);
	void DispatchCommand(uint32_t source, const char* msg, int sz);

private:
	void dispatchInotifyEvent(const inotify_event *ev, size_t data_sz);
	void addWatchPath(uint32_t service_handle, const std::string& watch_path, uint32_t watch_events);
	void rmWatchPath(uint32_t service_handle, const std::string& watch_path);
	void rmWatchService(uint32_t service_handle);

private:
	struct WatchEntry {
		int wd_;
		// service_handle : watch_events
		std::unordered_map<uint32_t, uint32_t> services_;
	};
	// watch_path : WatchEntry
	std::unordered_map<std::string, WatchEntry> watchers_;
	// wd : watch_path
	std::unordered_map<int, std::string> wd_paths_;

	skynet_context *ctx_;
	int inotify_fd_;
	// skynet bind socket id
	int inotify_sid_;
	// 虽然inotify保证写入数据的完整性, 但skynet框架socket read读取长度是随机的
	std::string recv_buffer_;
};

} // namespace fsnotify
} // namespace skynet_ext