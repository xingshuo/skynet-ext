#pragma once
#include <string>
#include <stdint.h>

extern "C" {
#include "skynet_server.h"
#include "skynet.h"
#include "skynet_mq.h"
}

struct TestFSNotify {
public:
	void Init(skynet_context *ctx) {
		m_ctx = ctx;
	}
	void AddWatchPath(const std::string& path, uint32_t events_mask);
	void RmWatchPath(const std::string& path);
	void RmWatchService();

private:
	void runCmd(const std::string& cmd);

private:
	skynet_context *m_ctx;
};
