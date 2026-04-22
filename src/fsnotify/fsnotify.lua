local Skynet = require "skynet"
local lfsnotify = require "lfsnotify"

local PTYPE_FSNOTIFY = 18 -- must be same with define in `fsnotify/common.h`
assert(Skynet[PTYPE_FSNOTIFY] == nil, "skynet protocol type conflict")


local fsnotifyHandlers = {}

local function dispatchFSNotify(session, source, watchPath, inotifyEvent)
	if watchPath then
		local handler = fsnotifyHandlers[watchPath]
		if handler then
			handler(watchPath, inotifyEvent)
		end
	end
end

local M = {}

function M.RegisterProtocol()
	Skynet.register_protocol {
		name = "fsnotify",
		id = PTYPE_FSNOTIFY,
		pack = function (...)
			return table.concat({...}," ")
		end,
		unpack = function (...)
			return lfsnotify.Filter(...)
		end,
		dispatch = dispatchFSNotify,
	}
end

function M.RegisterHandler(watchPath, eventsMask, handler)
	assert(eventsMask and eventsMask ~= 0, eventsMask)
	-- NOTICE: watchPath中不能包含' '
	assert(watchPath:find(" ") == nil, watchPath)
	assert(handler, watchPath)
	local originHandler = fsnotifyHandlers[watchPath]
	fsnotifyHandlers[watchPath] = handler
	Skynet.send(".fsnotify", "fsnotify", "AddWatchPath", watchPath, eventsMask)
	return originHandler
end

function M.UnregisterHandler(watchPath)
	fsnotifyHandlers[watchPath] = nil
	Skynet.send(".fsnotify", "fsnotify", "RmWatchPath", watchPath)
end

function M.UnregisterAllHandlers()
	fsnotifyHandlers = {}
	Skynet.send(".fsnotify", "fsnotify", "RmWatchService")
end

return M