#pragma once
#include <unordered_map>

extern "C" {
#include "skynet_server.h"
#include "skynet.h"
#include "skynet_mq.h"
}

struct TestSignal {
public:
	void Init(skynet_context *ctx) {
		m_ctx = ctx;
	};
	void RunTest(int signum);
	void RunCmd(const char *cmd, const char *parm = nullptr);

private:
	skynet_context *m_ctx;
};