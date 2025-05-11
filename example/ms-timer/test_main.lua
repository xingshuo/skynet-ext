local Skynet = require "skynet"
require "skynet.manager"

Skynet.start(function()
	local handle = Skynet.launch("testtm")
	assert(handle, "launch testtm service failed")

	Skynet.exit()
end)