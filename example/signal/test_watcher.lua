local Skynet = require "skynet"
local Signal = require "signal"

local iNo = ...
iNo = math.floor(tonumber(iNo))

Skynet.start(function()
	Signal.RegisterProtocol()

	Signal.RegisterHandler(Signal.SIGUSR1, function (signum)
		assert(signum == Signal.SIGUSR1)
		Skynet.error("recv signal callback 111 ", Signal.GetName(signum), signum, "on service", iNo)
		Signal.UnregisterHandler(signum)
		Signal.DebugInfo()
	end)

	Signal.RegisterHandler(Signal.SIGUSR2, function(signum)
		assert(signum == Signal.SIGUSR2)
		Skynet.error("recv signal callback 222 ", Signal.GetName(signum), signum, "on service", iNo)
		Signal.UnregisterHandler(signum)
		Signal.DebugInfo()
	end)
end)