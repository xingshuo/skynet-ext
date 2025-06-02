local Skynet = require "skynet"

local PTYPE_SIGNAL = 16
assert(Skynet[PTYPE_SIGNAL] == nil, "skynet protocol type conflict")

-- Reference: https://www.chromium.org/chromium-os/developer-library/reference/linux-constants/signals/
local SignalTable = {
	SIGHUP = 1,
	SIGINT = 2,
	SIGQUIT = 3,
	SIGILL = 4,
	SIGTRAP = 5,
	SIGABRT = 6,
	SIGIOT = 6,
	SIGBUS = 7,
	SIGFPE = 8,
	SIGKILL = 9,
	SIGUSR1 = 10,
	SIGSEGV = 11,
	SIGUSR2 = 12,
	SIGPIPE = 13,
	SIGALRM = 14,
	SIGTERM = 15,
	SIGSTKFLT = 16,
	SIGCHLD = 17,
	SIGCONT = 18,
	SIGSTOP = 19,
	SIGTSTP = 20,
	SIGTTIN = 21,
	SIGTTOU = 22,
	SIGURG = 23,
	SIGXCPU = 24,
	SIGXFSZ = 25,
	SIGVTALRM = 26,
	SIGPROF = 27,
	SIGWINCH = 28,
	SIGIO = 29,
	SIGPOLL = 29, -- equal to SIGIO
	SIGPWR = 30,
	SIGSYS = 31,
}

local M = {}

for signame, signum in pairs(SignalTable) do
	M[signame] = signum
end

local illegalSignals = {
	-- skynet use
	SIGPIPE = 13,
	SIGHUP = 1,
	-- uncatchable
	SIGKILL = 9,
	SIGSTOP = 19,
	-- abnormal
	SIGSEGV = 11,
	SIGILL = 4, -- Illegal instruction
	SIGFPE = 8, -- Floating point exception
	SIGABRT = 6,
	SIGIOT = 6,
	SIGBUS = 7, -- Bus error
}

local MinSigNum = 1
local MaxSigNum = 64

local function isLegalSignal(signum)
	for name, val in pairs(SignalTable) do
		if signum == val then
			return not illegalSignals[name]
		end
	end
	return false
end

local signalHandlers = {}

local function dispatchSignal(session, source, msg, sz)
	local signum = sz
	assert(MinSigNum <= signum and signum <= MaxSigNum, signum)
	local handler = signalHandlers[signum]
	if handler then
		Skynet.error("dispatch signal handler: ", signum)
		handler(signum)
	end
end

function M.RegSignalProtocol()
	Skynet.register_protocol {
		name = "signal",
		id = PTYPE_SIGNAL,
		pack = function (...)
			return table.concat({...}," ")
		end,
		unpack = function (...)
			return ...
		end,
		dispatch = dispatchSignal,
	}
end

function M.RegSignalHandler(signum, handler)
	assert(isLegalSignal(signum), signum)
	local original = signalHandlers[signum]
	signalHandlers[signum] = assert(handler, signum)
	Skynet.send(".signal_mgr", "signal", "RegisterWatcher", signum)
	Skynet.error("register signal handler: ", signum)
	return original
end

function M.UnregSignalHandler(signum)
	signum = signum or 0
	if signalHandlers[signum] then
		signalHandlers[signum] = nil
	end
	Skynet.error("unregister signal handler: ", signum)
	Skynet.send(".signal_mgr", "signal", "UnregisterWatcher", signum)
end

function M.GetSignalName(signum)
	for name, val in pairs(SignalTable) do
		if signum == val then
			return name
		end
	end
end

return M