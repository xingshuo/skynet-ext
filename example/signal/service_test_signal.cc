#include <assert.h>
#include <time.h>
#include <unistd.h>
#include <string.h>
#include <string>
#include <signal.h>
#include <cstdio>

#include "service_test_signal.h"
#include "signal/common.h"

static skynet_context *g_sig_ctx = nullptr;

void TestSignal::RunTest(int signum) {
	char sig[32];
	sprintf(sig, "%d", signum);
	RunCmd("RegisterWatcher", sig);
}

void TestSignal::RunCmd(const char *cmd, const char *parm) {
	size_t cmd_len = strlen(cmd);
	size_t sz;
	char *data = nullptr;
	if (parm != nullptr) {
		sz = cmd_len + 1 + strlen(parm);
		data = (char *)skynet_malloc(sz + 1);
		strcpy(data, cmd);
		data[cmd_len] = ' ';
		strcpy(data + cmd_len + 1, parm);
	} else {
		sz = cmd_len;
		data = (char *)skynet_malloc(sz + 1);
		strcpy(data, cmd);
	}
	struct skynet_message message;
	message.source = skynet_context_handle(m_ctx);
	message.session = 0;
	message.data = data;
	message.sz = sz | (size_t)PTYPE_SIGNAL << MESSAGE_TYPE_SHIFT;
	skynet_context_push(skynet_context_handle(g_sig_ctx), &message);
	skynet_error(m_ctx, "run cmd: %s, parm: %s", cmd, parm);
}

static int
_cb(skynet_context *ctx, void *ud, int type, int session, uint32_t source, const void *msg, size_t sz) {
	TestSignal *app = (TestSignal *)ud;
	switch(type) {
	case PTYPE_SIGNAL:
		skynet_error(ctx, "recv signal callback signum: %d", (int)sz);
		char sig[32];
		sprintf(sig, "%d", (int)sz);
		app->RunCmd("UnregisterWatcher", sig);
		app->RunCmd("DebugInfo");
		break;
	}
	return 0;
}

extern "C" TestSignal*
test_signal_create(void) {
	return new TestSignal;
}

extern "C" void
test_signal_release(TestSignal* app) {
	delete app;
}

extern "C" int
test_signal_init(TestSignal *app, skynet_context *ctx, char *parm) {
	if (parm == nullptr || parm[0] == '\0' || strcmp(parm, "master") == 0) {
		g_sig_ctx = skynet_context_new("signal_mgr", nullptr);
		skynet_callback(ctx, app, _cb);
		app->Init(ctx);
		skynet_error(ctx, "test-signal: master service init handle :%x", skynet_context_handle(g_sig_ctx));
		for (int i=1; i<=2; i++) {
			char args[32];
			sprintf(args, "%d", i);
			skynet_context *agent = skynet_context_new("test_signal", args);
			assert(agent != nullptr);
		}
	} else {
		skynet_callback(ctx, app, _cb);
		app->Init(ctx);
		skynet_error(ctx, "test-signal: agent service init parm: %s handle: :%x", parm, skynet_context_handle(ctx));
		app->RunTest(SIGUSR1);
		app->RunTest(SIGUSR2);
	}
	return 0;
}