#include <assert.h>
#include <time.h>
#include <unistd.h>

#include "service_testtimer.h"
#include "ms-timer/api.h"


static skynet_context *g_ctx = nullptr;

TestApp::TestApp() {

}

TestApp::~TestApp() {
	skynet_ext::ms_timer::ExitPoller();
}

void TestApp::Init(skynet_context *ctx) {
	m_ctx = ctx;

	int session = 100;
	int count = 1;
	int interval_ms = 33;
	StartTimer(session, count, interval_ms);

	session++;
	interval_ms = 17;
	StartTimer(session, 2, interval_ms);

	session++;
	interval_ms = 60;
	StartTimer(session, count, interval_ms);

	session++;
	interval_ms = 105;
	StartTimer(session, count, interval_ms);

	// session++;
	// interval_ms = 17;
	// StartTimer(session, 0, interval_ms);

	session++;
	interval_ms = 1;
	StartTimer(session, count, interval_ms);
}

void TestApp::StartTimer(int session, int count, int interval_ms) {
	timespec now;
	clock_gettime(CLOCK_MONOTONIC, &now);
	skynet_ext::ms_timer::StartTimer(skynet_context_handle(m_ctx), session, count, interval_ms);
	skynet_error(m_ctx, "test-mstimer: start timer session: %d dt: %d at (%ld, %ld)", session, interval_ms, now.tv_sec, now.tv_nsec);
	m_sessions[session] = {now, count};
}

void TestApp::OnCallback(int session) {
	timespec now;
	clock_gettime(CLOCK_MONOTONIC, &now);
	auto iter = m_sessions.find(session);
	if (iter == m_sessions.end()) {
		return;
	}
	auto& sessionCtx = iter->second;
	skynet_error(m_ctx, "test-mstimer: get response session: %d at:(%ld, %ld) dt:(%ld, %ld)", session, now.tv_sec, now.tv_nsec, now.tv_sec - sessionCtx.time.tv_sec, now.tv_nsec - sessionCtx.time.tv_nsec);
	sessionCtx.count--;
	if (sessionCtx.count == 0) {
		m_sessions.erase(session);
		if (m_sessions.size() == 0) {
			skynet_command(m_ctx, "EXIT", nullptr);
		}
	}
}

extern "C" int
_cb(skynet_context *ctx, void *ud, int type, int session, uint32_t source, const void *msg, size_t sz) {
	TestApp *app = (TestApp *)ud;
	switch(type) {
	case PTYPE_RESPONSE:
		app->OnCallback(session);
		break;
	}
	return 0;
}

extern "C" TestApp*
testtm_create(void) {
	TestApp *app = new TestApp();
	return app;
}

extern "C" void
testtm_release(TestApp* app) {
	if (*(skynet_context **)app == g_ctx) {
		skynet_ext::ms_timer::ExitPoller();
	}
	delete app;
}

extern "C" int
testtm_init(TestApp *app, skynet_context *ctx, char *parm) {	
	if (parm == nullptr || parm[0] == '\0') {
		int ret = skynet_ext::ms_timer::InitPoller(4);
		assert(ret == skynet_ext::ms_timer::ErrCode::OK);
		skynet_callback(ctx, app, _cb);
		app->Init(ctx);	
		skynet_error(ctx, "test-mstimer: bootstrap service init");
		g_ctx = ctx;
		for (int i = 0; i < 3; i++) {
			char args[32];
			sprintf(&args[0], "%d", i);
			skynet_context *agent = skynet_context_new("testtm", args);
			assert(agent != nullptr);
		}
		skynet_error(ctx, "test-mstimer: bootstrap service done");
	} else {
		skynet_callback(ctx, app, _cb);
		app->Init(ctx);
		skynet_error(ctx, "test-mstimer: agent service init: %s", parm);
	}
	return 0;
}