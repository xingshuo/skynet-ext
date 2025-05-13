local Skynet = require "skynet"
require "skynet.manager"
local Lmstimer = require "lmstimer"

local session = 0
local timers_map = {}

local function timer_timeout(ti, func, count)
	assert(ti > 0)
	count = count or 0
	session = session - 1
	if session > 0 then -- skynet use positive number
		session = -1
	end
	local ret = Lmstimer.StartTimer(Skynet.self(), session, count, ti)
	assert(Lmstimer.ErrCode.OK == ret, "timeout failed:".. Lmstimer.ErrCode[ret])
	timers_map[session] = {func = func, count = count}
	return session
end

local old_unknown_response
local function timer_callback(session, source, msg, sz)
	print("on timer callback:", session, source)
	local ctx = timers_map[session]
	if not ctx then
		old_unknown_response(session, source, msg, sz)
		return
	end
	if ctx.count > 0 then
		ctx.count = ctx.count - 1
		if ctx.count == 0 then
			timers_map[session] = nil
		end
	end

	Skynet.fork(ctx.func)
end

-- Black Tech, Only For Test!
old_unknown_response = Skynet.dispatch_unknown_response(timer_callback)

Skynet.start(function()
	for k,v in pairs(Lmstimer.ErrCode) do
		print("k:", k, "v:", v)
	end
	local ret = Lmstimer.InitPoller(2)
	assert(Lmstimer.ErrCode.OK == ret, "init poller failed:".. Lmstimer.ErrCode[ret])
	local await_token = "qwerty"
	local count = 0
	local session
	session = timer_timeout(23, function ()
		count = count + 1
		print("timeout: ", count)
		if count >= 2 then
			Lmstimer.StopTimer(Skynet.self(), session)
			Skynet.wakeup(await_token)
		end
	end)
	Skynet.wait(await_token)
	Lmstimer.ExitPoller()

	-- local handle = Skynet.launch("testtm")
	-- assert(handle, "launch testtm service failed")

	Skynet.exit()
end)