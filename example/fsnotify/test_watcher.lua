local Skynet = require "skynet"
require "skynet.manager"
local FSNotify = require "fsnotify"
local Lfsnotify = require "lfsnotify"

local iNo = ...
iNo = tonumber(iNo) or 0

-- 三个 watcher 订阅同一目录, 用不同 mask 验证过滤
local SHARED_DIR = "/tmp/skynet_fsnotify_lua_test"

local EM = Lfsnotify.EventMask
local maskPerSvc = {
	[1] = EM.IN_CREATE,                                        -- 只关心 CREATE
	[2] = EM.IN_DELETE,                                        -- 只关心 DELETE
	[3] = EM.IN_CREATE | EM.IN_MODIFY | EM.IN_DELETE,          -- 全量
}

local subscribableBits = {
	"IN_ATTRIB", "IN_CREATE", "IN_MODIFY", "IN_DELETE",
	"IN_DELETE_SELF", "IN_MOVE_SELF", "IN_MOVED_FROM",
	"IN_MOVED_TO", "IN_ISDIR", "IN_IGNORED",
}

local function formatMask(mask)
	local names = {}
	for _, key in ipairs(subscribableBits) do
		local v = EM[key]
		if v and (mask & v) == v then
			names[#names+1] = key
		end
	end
	return table.concat(names, "|")
end

Skynet.start(function()
	FSNotify.RegisterProtocol()
	os.execute("mkdir -p " .. SHARED_DIR)

	local myMask = assert(maskPerSvc[iNo], "unknown svc iNo: " .. iNo)
	FSNotify.RegisterHandler(SHARED_DIR, myMask, function(watchPath, ev)
		Skynet.error(string.format(
			"[svc %d sub=%s] recv event: path=%s name=%s ev.mask=0x%x(%s)",
			iNo, formatMask(myMask), watchPath, ev.name or "", ev.mask, formatMask(ev.mask)))
	end)
	Skynet.error(string.format("[svc %d] subscribed %s with mask=0x%x(%s)",
		iNo, SHARED_DIR, myMask, formatMask(myMask)))

	if iNo ~= 3 then
		return -- svc 1/2 只订阅, 事件由 svc 3 生产
	end

	-- 等 svc1/svc2 的 AddWatchPath 消息先抵达 fsnotify
	Skynet.sleep(30)
	-- 输出 DebugInfo
	FSNotify.OutputWatchInfo()
	Skynet.sleep(30)

	local file = SHARED_DIR .. "/shared.txt"
	-- 期望: svc1 收到, svc3 收到; svc2 不收 (只订阅 DELETE)
	Skynet.error("[svc 3] ----- produce CREATE -----")
	os.execute("touch " .. file)
	Skynet.sleep(30)

	-- 期望: svc3 收到; svc1/svc2 不收 (svc1 只订阅 CREATE, svc2 只订阅 DELETE)
	Skynet.error("[svc 3] ----- produce MODIFY -----")
	os.execute("echo hello > " .. file)
	Skynet.sleep(30)

	-- 期望: svc2/svc3 都收到; svc1 不收
	Skynet.error("[svc 3] ----- produce DELETE -----")
	os.execute("rm -f " .. file)
	Skynet.sleep(100)

	-- 验证 UnregisterHandler: svc 3 注销 SHARED_DIR, 之后事件只有 svc1/svc2 才可能收到
	Skynet.error("[svc 3] UnregisterHandler, next events should NOT go to svc 3")
	FSNotify.UnregisterHandler(SHARED_DIR)
	-- 输出 DebugInfo
	FSNotify.OutputWatchInfo()
	Skynet.sleep(30)
	os.execute("touch " .. file)   -- svc1 应收到
	Skynet.sleep(30)
	os.execute("rm -f " .. file)   -- svc2 应收到
	Skynet.sleep(100)

	-- 验证 UnregisterAllHandlers: 理论上清理服务端所有订阅 (svc 3 已无订阅, 主要覆盖接口路径)
	Skynet.error("[svc 3] UnregisterAllHandlers")
	FSNotify.UnregisterAllHandlers()
	Skynet.sleep(30)

	-- 清理目录
	Skynet.error("[svc 3] ----- produce DELETE DIR -----")
	os.execute("rm -rf " .. SHARED_DIR)
	Skynet.sleep(100)

	Skynet.error("[svc 3] test done")
end)
