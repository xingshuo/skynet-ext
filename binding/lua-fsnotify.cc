#include <stdint.h>
#include <sys/inotify.h>
#include "lua.hpp"

static int lb_Filter(lua_State *L) {
	void *msg = lua_touserdata(L, 1);
	size_t size = lua_tointeger(L, 2);
	if (size < sizeof(inotify_event)) {
		return luaL_error(L, "fsnotify payload size (%zu) < event_sz", size);
	}
	inotify_event *ev = static_cast<inotify_event *>(msg);
	if (size - sizeof(inotify_event) < ev->len) {
		return luaL_error(L, "fsnotify size(%zu) / ev->len(%u) mismatch", size, ev->len);
	}
	size_t data_sz = sizeof(inotify_event) + ev->len;
	// watchPath
	char *wpath = (char *)msg + data_sz;
	lua_pushlstring(L, wpath, size - data_sz);
	// inotifyEvent
	lua_createtable(L, 0, 4);
	lua_pushinteger(L, ev->wd);
	lua_setfield(L, -2, "wd");
	lua_pushinteger(L, ev->mask);
	lua_setfield(L, -2, "mask");
	lua_pushinteger(L, ev->cookie);
	lua_setfield(L, -2, "cookie");
	if(ev->len > 0) {
		lua_pushstring(L, ev->name);
		lua_setfield(L, -2, "name");
	}
	return 2;
}

#define registerInotifyEventMask(s) do { \
	lua_pushinteger(L, s); \
	lua_setfield(L, -2, #s); \
	lua_pushstring(L, #s); \
	lua_seti(L, -2, s); \
} while (0)

extern "C" int
luaopen_lfsnotify(lua_State *L)
{
	luaL_checkversion(L);
	luaL_Reg l[] = {
		{"Filter", lb_Filter},
		{NULL, NULL}
	};
	luaL_newlib(L, l);
	// export inotify event mask defines
	lua_newtable(L);
	// 订阅"事件位"
	registerInotifyEventMask(IN_ATTRIB);
	registerInotifyEventMask(IN_CREATE);
	registerInotifyEventMask(IN_MODIFY);
	registerInotifyEventMask(IN_DELETE);
	registerInotifyEventMask(IN_DELETE_SELF);
	registerInotifyEventMask(IN_MOVE_SELF);
	registerInotifyEventMask(IN_MOVED_FROM);
	registerInotifyEventMask(IN_MOVED_TO);
	// 属性/状态位
	registerInotifyEventMask(IN_ISDIR);
	registerInotifyEventMask(IN_IGNORED);
	lua_setfield(L, -2, "EventMask");

	return 1;
}