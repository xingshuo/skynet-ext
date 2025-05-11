#include <assert.h>
#include <time.h>
#include <unistd.h>

#include "service_testtimer.h"
#include "include/mstimer.h"


TestApp::TestApp() {

}

TestApp::~TestApp() {
	skynet_ext::ms_timer::ExitPoller();
}

void TestApp::Init(skynet_context *ctx) {
	m_ctx = ctx;
	int ret = skynet_ext::ms_timer::InitPoller(4);
	assert(ret == skynet_ext::ms_timer::ErrCode::OK);
	sleep(2);

	int session = 100;
	int count = 1;
	int interval_ms = 33;
	StartTimer(session, count, interval_ms);

	session++;
	interval_ms = 17;
	StartTimer(session, count, interval_ms);

	session++;
	interval_ms = 60;
	StartTimer(session, count, interval_ms);

	session++;
	interval_ms = 105;
	StartTimer(session, count, interval_ms);

	session++;
	interval_ms = 17;
	StartTimer(session, count, interval_ms);

	session++;
	interval_ms = 1;
	StartTimer(session, count, interval_ms);
}

void TestApp::StartTimer(int session, int count, int interval_ms) {
	timespec now;
	clock_gettime(CLOCK_MONOTONIC, &now);
	skynet_ext::ms_timer::StartTimer(skynet_context_handle(m_ctx), session, count, interval_ms);
	skynet_error(m_ctx, "test-mstimer: start timer session: %d dt: %d at (%ld, %ld)", session, interval_ms, now.tv_sec, now.tv_nsec);
	m_sessions[session] = now;
}

void TestApp::OnCallback(int session) {
	timespec now;
	clock_gettime(CLOCK_MONOTONIC, &now);
	auto iter = m_sessions.find(session);
	if (iter == m_sessions.end()) {
		return;
	}
	auto& starttime = iter->second;
	skynet_error(m_ctx, "test-mstimer: get response session: %d at:(%ld, %ld) dt:(%ld, %ld)", session, now.tv_sec, now.tv_nsec, now.tv_sec - starttime.tv_sec, now.tv_nsec - starttime.tv_nsec);
	m_sessions.erase(session);
	if (m_sessions.size() == 0) {
		skynet_command(m_ctx, "EXIT", nullptr);
	}
}

extern "C" int
_cb(skynet_context * ctx, void * ud, int type, int session, uint32_t source, const void * msg, size_t sz) {
	TestApp *app = (TestApp *)ud;
	switch(type) {
	case 1:
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
	skynet_ext::ms_timer::ExitPoller();
	delete app;
}

extern "C" int
testtm_init(TestApp *app, skynet_context *ctx, char *parm) {
	app->Init(ctx);	
	skynet_callback(ctx, app, _cb);
	skynet_error(ctx, "test-mstimer: service init done");
	return 0;
}