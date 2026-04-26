local Skynet = require "skynet"
require "skynet.manager"

Skynet.start(function()
	local dcPort = Skynet.getenv("debug_console_port")
	if dcPort then
		Skynet.newservice("debug_console", dcPort)
	end
	local handle = Skynet.launch("fsnotify")
	assert(handle, "launch fsnotify service failed")

	-- svc 1/2: 订阅同一目录但使用不同 mask (验证多订阅者 + mask 过滤)
	-- svc 3:   订阅全量 mask + 作为事件生产者 (验证 UnregisterAllHandlers)
	Skynet.newservice("test_watcher", 1)
	Skynet.newservice("test_watcher", 2)
	Skynet.newservice("test_watcher", 3)

	-- 退出fsnotify服务
	Skynet.kill(".fsnotify")
	Skynet.sleep(100)

	Skynet.exit()
end)
