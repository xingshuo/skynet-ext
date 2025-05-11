#pragma once

#include <unordered_map>
// #include <chrono>
// #include <ctime>

extern "C" {
#include "skynet_server.h"

void skynet_error(struct skynet_context* context, const char* msg, ...);
const char * skynet_command(struct skynet_context * context, const char * cmd , const char * parm);
typedef int (*skynet_cb)(struct skynet_context * context, void *ud, int type, int session, uint32_t source , const void * msg, size_t sz);
void skynet_callback(struct skynet_context * context, void *ud, skynet_cb cb);
}

struct TestApp {
public:
	TestApp();
	~TestApp();

	void Init(skynet_context *ctx);
	void StartTimer(int session, int count, int interval_ms);
	void OnCallback(int session);

private:
	skynet_context *m_ctx;
	std::unordered_map<int, timespec> m_sessions;
	
};