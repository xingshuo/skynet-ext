#pragma once
#include <unordered_map>

extern "C" {
#include "skynet_server.h"
#include "skynet.h"
}

struct SessionCtx {
	timespec time;
	int count;
};

struct TestMSTimer {
public:
	void Init(skynet_context *ctx) {
		m_ctx = ctx;
	};
	void RunTest();
	void StartTimer(int session, int count, int interval_ms);
	void OnCallback(int session);
	const skynet_context *GetContext() {
		return m_ctx;
	}

private:
	skynet_context *m_ctx;
	std::unordered_map<int, SessionCtx> m_sessions;
	
};