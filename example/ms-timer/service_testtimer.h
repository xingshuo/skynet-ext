#pragma once

#include <unordered_map>

extern "C" {
#include "skynet.h"
#include "skynet_server.h"
}

struct SessionCtx {
	timespec time;
	int count;
};

struct TestApp {
public:
	TestApp();
	~TestApp();

	void Init(skynet_context *ctx);
	void StartTimer(int session, int count, int interval_ms);
	void OnCallback(int session);

private:
	skynet_context *m_ctx;
	std::unordered_map<int, SessionCtx> m_sessions;
	
};