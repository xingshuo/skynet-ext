#include <stdint.h>

#include "lua.hpp"
#include "ms-timer/api.h"

static int lb_InitPoller(lua_State *L) {
	uint32_t nthreads = static_cast<uint32_t>(luaL_checkinteger(L, 1));
	int ec = skynet_ext::ms_timer::InitPoller(nthreads);
	lua_pushinteger(L, ec);
	return 1;
}

static int lb_ExitPoller(lua_State *L) {
	(void)L;
	skynet_ext::ms_timer::ExitPoller();
	return 0;
}

static int lb_StartMSTimer(lua_State *L) {
	uint32_t service_handle = static_cast<uint32_t>(luaL_checkinteger(L, 1));
	int session = luaL_checkinteger(L, 2);
	int count = luaL_checkinteger(L, 3);
	int64_t interval_ms = static_cast<int64_t>(luaL_checkinteger(L, 4));
	if (interval_ms <= 0 || interval_ms > (int64_t)(INT64_MAX / 1000000)) {
		return luaL_error(L, "ms-timer: invalid ms interval (%ld) node (%u, %d)", interval_ms, service_handle, session);
	}
	int64_t interval_ns = interval_ms * 1000000;
	int ec = skynet_ext::ms_timer::StartTimer(service_handle, session, count, interval_ns);
	lua_pushinteger(L, ec);
	return 1;
}

static int lb_StartNSTimer(lua_State *L) {
	uint32_t service_handle = static_cast<uint32_t>(luaL_checkinteger(L, 1));
	int session = luaL_checkinteger(L, 2);
	int count = luaL_checkinteger(L, 3);
	int64_t interval_ns = static_cast<int64_t>(luaL_checkinteger(L, 4));
	if (interval_ns <= 0) {
		return luaL_error(L, "ms-timer: invalid ns interval (%ld) node (%u, %d)", interval_ns, service_handle, session);
	}
	int ec = skynet_ext::ms_timer::StartTimer(service_handle, session, count, interval_ns);
	lua_pushinteger(L, ec);
	return 1;
}

static int lb_StopTimer(lua_State *L) {
	uint32_t service_handle = static_cast<uint32_t>(luaL_checkinteger(L, 1));
	int session = luaL_checkinteger(L, 2);
	skynet_ext::ms_timer::StopTimer(service_handle, session);
	return 0;
}

#define registerErrCode(s)\
	lua_pushinteger(L, skynet_ext::ms_timer::s);\
	lua_setfield(L, -2, #s);\
	lua_pushstring(L, #s);\
	lua_seti(L, -2, skynet_ext::ms_timer::s)

extern "C" int
luaopen_lmstimer(lua_State *L)
{
	luaL_checkversion(L);
	luaL_Reg l[] = {
		{"InitPoller", lb_InitPoller},
		{"ExitPoller", lb_ExitPoller},
		{"StartMSTimer", lb_StartMSTimer},
		{"StartNSTimer", lb_StartNSTimer},
		{"StopTimer", lb_StopTimer},
		{NULL, NULL}
	};
	luaL_newlib(L, l);

	lua_newtable(L);
	registerErrCode(OK);
	registerErrCode(POLLER_CREATE_ERROR);
	registerErrCode(PIPE_CREATE_ERROR);
	registerErrCode(POLLER_ADD_ERROR);
	registerErrCode(TIMERFD_CREATE_ERROR);
	registerErrCode(TIMERFD_SET_NONBLOCK_ERROR);
	registerErrCode(API_PARAM1_ERROR);
	registerErrCode(API_PARAM2_ERROR);
	registerErrCode(API_PARAM3_ERROR);
	registerErrCode(API_PARAM4_ERROR);
	registerErrCode(UNKNOWN_ERROR);
	lua_setfield(L, -2, "ErrCode");

	return 1;
}