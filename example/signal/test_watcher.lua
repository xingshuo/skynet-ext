local Skynet = require "skynet"
local Signal = require "signal"

local iNo = ...
iNo = math.floor(tonumber(iNo))

Skynet.start(function()
	Signal.RegSignalProtocol()

	Signal.RegSignalHandler(Signal.SIGUSR1, function (signum)
		assert(signum == Signal.SIGUSR1)
		Skynet.error("recv signal callback ", Signal.GetSignalName(signum), signum, "on service", iNo)
		Signal.UnregSignalHandler(signum)
	end)
end)