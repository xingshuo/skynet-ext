local Skynet = require "skynet"
require "skynet.manager"
local Lmstimer = require "lmstimer"

local cur_session = 0
local timers_map = {}
local PTYPE_MSTIMER = 16 -- must be same with define in `ms-timer/common.h`

local function timer_timeout(ti, func, count, session)
	assert(ti > 0)
	count = count or 0
	if not session then
		cur_session = cur_session - 1
		if cur_session > 0 then -- skynet use positive number
			cur_session = -1
		end
		session = cur_session
	end
	local ret = Lmstimer.StartTimer(Skynet.self(), session, count, ti)
	assert(Lmstimer.ErrCode.OK == ret, "timeout failed:".. Lmstimer.ErrCode[ret])
	timers_map[session] = {func = func, count = count}
	return session
end

local function timer_callback(session, source, msg, sz)
	Skynet.ignoreret()
	print("on timer callback:", session, source,  msg, sz)
	local ctx = timers_map[session]
	if not ctx then
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

Skynet.start(function()
	Skynet.register_protocol {
		name = "mstimer",
		id = PTYPE_MSTIMER,
		pack = function (...) end,
		unpack = function (...)
			return ...
		end,
		dispatch = timer_callback,
	}

	print("====ErrCode dump begin!======")
	for k,v in pairs(Lmstimer.ErrCode) do
		print(k, " : ", v)
	end
	print("====ErrCode dump end!======")

	local ret = Lmstimer.InitPoller(2)
	assert(Lmstimer.ErrCode.OK == ret, "init poller failed:".. Lmstimer.ErrCode[ret])
	local await_token = "qwerty"
	local count = 0
	local session
	session = timer_timeout(30, function ()
		count = count + 1
		print("timeout: aaaa ", count, session)
		if count >= 2 then
			-- test remove timer
			Lmstimer.StopTimer(Skynet.self(), session)

			local session2
			session2 = timer_timeout(1000, function()
				print("timeout: bbbb", session2)
				Skynet.wakeup(await_token)
			end)

			-- for repeat session test
			timer_timeout(50, function()
				print("timeout: cccc", session2)
				Skynet.wakeup(await_token)
			end, 1, session2)
		end
	end)

	Skynet.wait(await_token)
	Lmstimer.ExitPoller()

	Skynet.exit()
end)