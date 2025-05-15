#include <assert.h>
#include <time.h>
#include <unistd.h>
#include <string.h>
#include <cstdio>

#include "service_test_mstimer.h"
#include "ms-timer/api.h"


static skynet_context *g_ctx = nullptr;
static const int kPollerNum = 4;

void TestMSTimer::RunTest() {
	struct TimerCtx {
		int session;
		int count;
		int interval_ms;
	};
	int suffix = skynet_context_handle(m_ctx) % kPollerNum;
	TimerCtx timerTable[] = {
		{100+suffix, 1, 33},
		{200+suffix, 2, 17},
		{300+suffix, 1, 60},
		{400+suffix, 1, 105},
		{500+suffix, 1, 2},
	};

	for (uint32_t i = 0; i < sizeof(timerTable)/sizeof(timerTable[0]); i++) {
		StartTimer(timerTable[i].session, timerTable[i].count, timerTable[i].interval_ms);
	}
}

void TestMSTimer::StartTimer(int session, int count, int interval_ms) {
	timespec now;
	clock_gettime(CLOCK_MONOTONIC, &now);
	skynet_ext::ms_timer::StartTimer(skynet_context_handle(m_ctx), session, count, interval_ms);
	skynet_error(m_ctx, "test-mstimer: start timer session: %d interval: %dms at (%lds, %ldns) count: %d", session, interval_ms, now.tv_sec, now.tv_nsec, count);
	m_sessions[session] = {now, count};
}

void TestMSTimer::OnCallback(int session) {
	timespec now;
	clock_gettime(CLOCK_MONOTONIC, &now);
	auto iter = m_sessions.find(session);
	if (iter == m_sessions.end()) {
		return;
	}
	auto& sessionCtx = iter->second;

	long sub_sec = now.tv_sec - sessionCtx.time.tv_sec;
	long sub_nsec = now.tv_nsec - sessionCtx.time.tv_nsec;
	if (sub_nsec < 0) {
		sub_sec--;
		sub_nsec += 1000000000;
	}
	double sub_msec = double(sub_nsec)/1000000.0;
	skynet_error(m_ctx, "test-mstimer: get response session: %d at:(%lds, %ldns) dt:(%lds, %fms)", session, now.tv_sec, now.tv_nsec, sub_sec, sub_msec);
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
	TestMSTimer *app = (TestMSTimer *)ud;
	switch(type) {
	case PTYPE_RESPONSE:
		app->OnCallback(session);
		break;
	}
	return 0;
}

extern "C" TestMSTimer*
test_mstimer_create(void) {
	TestMSTimer *app = new TestMSTimer();
	return app;
}

extern "C" void
test_mstimer_release(TestMSTimer* app) {
	if (app->GetContext() == g_ctx) {
		skynet_ext::ms_timer::ExitPoller();
	}
	delete app;
}

extern "C" int
test_mstimer_init(TestMSTimer *app, skynet_context *ctx, char *parm) {
	if (parm == nullptr || parm[0] == '\0' || strcmp(parm, "master") == 0) {
		int ret = skynet_ext::ms_timer::InitPoller(kPollerNum);
		assert(ret == skynet_ext::ms_timer::ErrCode::OK);
		skynet_callback(ctx, app, _cb);
		app->Init(ctx);
		skynet_error(ctx, "test-mstimer: master service init handle: :%x", skynet_context_handle(ctx));
		app->RunTest();
		g_ctx = ctx;
		for (int i=0; i<3; i++) {
			char args[32];
			sprintf(&args[0], "%d", i);
			skynet_context *agent = skynet_context_new("test_mstimer", args);
			assert(agent != nullptr);
		}
		skynet_error(ctx, "test-mstimer: master service done");
	} else {
		skynet_callback(ctx, app, _cb);
		app->Init(ctx);
		skynet_error(ctx, "test-mstimer: agent service init: %s handle: :%x", parm, skynet_context_handle(ctx));
		app->RunTest();
	}
	return 0;
}