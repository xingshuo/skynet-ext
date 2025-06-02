local Skynet = require "skynet"
require "skynet.manager"

Skynet.start(function()
	local dcPort = Skynet.getenv("debug_console_port")
	if dcPort then
		Skynet.newservice("debug_console", dcPort)
	end
	local handle = Skynet.launch("signal_mgr")
	assert(handle, "launch signal_mgr service failed")
	Skynet.newservice("test_watcher", 1)
	Skynet.newservice("test_watcher", 2)

	Skynet.exit()
end)