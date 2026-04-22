#include <cassert>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <fcntl.h>
#include <unistd.h>
#include <sys/inotify.h>

#include "service_test_fsnotify.h"
#include "fsnotify/common.h"


static skynet_context *g_fsnotify_ctx = nullptr;
static const char kWatchDir[] = "/tmp/skynet_fsnotify_cc_test";


void TestFSNotify::AddWatchPath(const std::string& path, uint32_t events_mask) {
	char buf[512];
	int n = snprintf(buf, sizeof(buf), "AddWatchPath %s %u", path.c_str(), events_mask);
	if (n <= 0 || n >= (int)sizeof(buf)) {
		skynet_error(m_ctx, "test-fsnotify: AddWatchPath build cmd failed, path: %s", path.c_str());
		return;
	}
	runCmd(std::string(buf, n));
}

void TestFSNotify::RmWatchPath(const std::string& path) {
	std::string cmd = "RmWatchPath ";
	cmd += path;
	runCmd(cmd);
}

void TestFSNotify::RmWatchService() {
	runCmd("RmWatchService");
}

void TestFSNotify::runCmd(const std::string& cmd) {
	size_t sz = cmd.size();
	char *data = (char *)skynet_malloc(sz + 1);
	memcpy(data, cmd.data(), sz);
	data[sz] = '\0';

	skynet_message message;
	message.source = skynet_context_handle(m_ctx);
	message.session = 0;
	message.data = data;
	message.sz = sz | (size_t)PTYPE_FSNOTIFY << MESSAGE_TYPE_SHIFT;
	skynet_context_push(skynet_context_handle(g_fsnotify_ctx), &message);
	skynet_error(m_ctx, "test-fsnotify: runCmd: %s", cmd.c_str());
}


static void produce_events(skynet_context *ctx, const char *dir) {
	char filepath[512];
	snprintf(filepath, sizeof(filepath), "%s/cc_file.txt", dir);

	skynet_error(ctx, "test-fsnotify: produce CREATE on %s", filepath);
	int fd = open(filepath, O_CREAT | O_WRONLY | O_TRUNC, 0644);
	if (fd < 0) {
		skynet_error(ctx, "test-fsnotify: open failed: %s", strerror(errno));
		return;
	}
	skynet_error(ctx, "test-fsnotify: produce MODIFY on %s", filepath);
	ssize_t n = write(fd, "hello", 5);
	(void)n;
	close(fd);

	skynet_error(ctx, "test-fsnotify: produce DELETE on %s", filepath);
	unlink(filepath);
}

static int
_cb(skynet_context *ctx, void *ud, int type, int session, uint32_t source, const void *msg, size_t sz) {
	(void)source;
	TestFSNotify *app = (TestFSNotify *)ud;
	switch (type) {
	case PTYPE_RESPONSE:
		// agent 启动后的延迟 timer 到期: 产生文件事件
		skynet_error(ctx, "test-fsnotify: timer fired (session:%d)", session);
		produce_events(ctx, kWatchDir);
		break;
	case PTYPE_FSNOTIFY: {
		if (sz < sizeof(inotify_event)) {
			skynet_error(ctx, "test-fsnotify: payload too small: %zu", sz);
			break;
		}
		const inotify_event *ev = static_cast<const inotify_event *>(msg);
		size_t data_sz = sizeof(inotify_event) + ev->len;
		if (sz < data_sz) {
			skynet_error(ctx, "test-fsnotify: payload incomplete: %zu < %zu", sz, data_sz);
			break;
		}
		size_t path_sz = sz - data_sz;
		std::string watch_path((const char *)msg + data_sz, path_sz);
		const char *name = (ev->len > 0) ? ev->name : "";
		skynet_error(ctx, "test-fsnotify: event path=%s wd=%d mask=0x%x cookie=%u name=%s",
			watch_path.c_str(), ev->wd, ev->mask, ev->cookie, name);
		// 收到 IN_DELETE 后主动清理订阅, 测试 RmWatchService 接口
		if (ev->mask & IN_DELETE) {
			skynet_error(ctx, "test-fsnotify: got IN_DELETE, call RmWatchService to cleanup");
			app->RmWatchService();
		}
		break;
	}
	}
	return 0;
}


extern "C" TestFSNotify*
test_fsnotify_create(void) {
	return new TestFSNotify;
}

extern "C" void
test_fsnotify_release(TestFSNotify *app) {
	delete app;
}

extern "C" int
test_fsnotify_init(TestFSNotify *app, skynet_context *ctx, char *parm) {
	if (parm == nullptr || parm[0] == '\0' || strcmp(parm, "master") == 0) {
		g_fsnotify_ctx = skynet_context_new("fsnotify", nullptr);
		assert(g_fsnotify_ctx != nullptr);
		skynet_callback(ctx, app, _cb);
		app->Init(ctx);
		skynet_error(ctx, "test-fsnotify: master init, fsnotify-handle: :%x", skynet_context_handle(g_fsnotify_ctx));

		// 启动 agent 订阅并产生事件
		char agent_arg[] = "agent";
		skynet_context *agent = skynet_context_new("test_fsnotify", agent_arg);
		assert(agent != nullptr);
	} else {
		skynet_callback(ctx, app, _cb);
		app->Init(ctx);
		skynet_error(ctx, "test-fsnotify: agent init (%s), handle: :%x", parm, skynet_context_handle(ctx));

		// 确保 watch 目录存在
		char mkdir_cmd[512];
		snprintf(mkdir_cmd, sizeof(mkdir_cmd), "mkdir -p %s", kWatchDir);
		int sys_ret = system(mkdir_cmd);
		(void)sys_ret;

		// 订阅 IN_CREATE | IN_MODIFY | IN_DELETE
		uint32_t mask = IN_CREATE | IN_MODIFY | IN_DELETE;
		app->AddWatchPath(kWatchDir, mask);

		// 200ms 后开始产生文件事件, 给 fsnotify 处理订阅留余量
		char timeout_arg[] = "20";
		skynet_command(ctx, "TIMEOUT", timeout_arg);
	}
	return 0;
}
