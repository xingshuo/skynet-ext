## 文件系统事件监听

## 平台支持
```
仅支持 Linux (依赖内核 inotify 子系统)
```

## 实现原理
* 基于 Linux 平台提供的 `inotify` 机制 + skynet 框架 socket 事件循环
```text
fsnotify 服务初始化时通过 inotify_init1 申请一个 inotify fd, 并用
skynet_socket_bind 将其托管给 skynet socket 线程, 由 epoll 驱动事件读取.

业务 service 通过 RegisterHandler 向 fsnotify 服务发送 AddWatchPath 命令;
fsnotify 服务内部调用 inotify_add_watch 建立内核 watch, 并维护
"watch_path -> {wd, {service_handle -> events_mask}}" 的路由表.

kernel 产生文件变更时, 事件经 inotify fd 送达 skynet socket 线程 ->
fsnotify 服务的 PTYPE_SOCKET 回调 -> 按 wd 查出关联的订阅者 -> 过滤
ev->mask 后, 将 "inotify_event 头 + watch_path" 作为 PTYPE_FSNOTIFY 消息
推送到对应订阅 service.
```
```text
┌────────────────────┐     AddWatchPath/RmWatchPath    ┌──────────────────────┐
│ service A/B/C ...  │────────────────────────────────▶│                      │
│ (订阅方)           │◀────────────────────────────────│  fsnotify service    │
│ PTYPE_FSNOTIFY     │     inotify_event + watch_path  │                      │
└────────────────────┘                                 │  - watchers_ 路由表  │
                                                       │  - recv_buffer_      │
                                                       └──────────┬───────────┘
                                                                  │
                                        skynet_socket_bind / start│PTYPE_SOCKET
                                                                  │
                                                      ┌───────────▼──────────┐
                                                      │  skynet socket thread│
                                                      │  epoll_wait(inotify) │
                                                      └───────────┬──────────┘
                                                                  │ read(inotify_fd)
                                                      ┌───────────▼──────────┐
                                                      │ linux kernel inotify │
                                                      │  fs/notify/inotify/  │
                                                      └──────────────────────┘
```

## 注意事项
* **WSL `/mnt` 挂载点上 inotify 不工作**: 通过 drvfs / 9P 挂载的 Windows 盘
  (如 `/mnt/c`, `/mnt/d`) 文件变更**不会触发** Linux VFS 的 fsnotify hook,
  表现为 `inotify_add_watch` 成功但事件永远不到达. 在 WSL 下开发请将项目
  放在 Linux 原生 fs (如 `~/`) 下, Windows 侧用 VSCode Remote-WSL 编辑即可.
* **编辑器的 atomic save 不触发 `IN_MODIFY`**: vim / vscode 默认保存是"写临时
  文件 -> rename 替换", 实际产生的是 `IN_MOVED_TO` / `IN_CREATE` 事件. 监听
  配置文件热更请同时订阅 `IN_MODIFY | IN_MOVED_TO | IN_CREATE`.
* **inotify 不递归监听子目录**: `inotify_add_watch` 只对传入路径本身建立 watch,
  业务侧如需递归需自行遍历目录树逐级订阅.
* **被监视对象被删除时**: kernel 会依次发 `IN_DELETE_SELF` / `IN_IGNORED`;
  fsnotify 服务在 `IN_IGNORED` 到达时会主动清理内部数据结构, 并向订阅者
  合成派发一个 `IN_IGNORED` 事件作为"watch 失效"通知 (无条件派发,
  不受订阅 mask 过滤).
* **内核事件队列溢出**: 高频变更场景下可能收到 `IN_Q_OVERFLOW`, 此时业务方
  应视为"可能丢事件", 执行全量对比或重新加载.

## Lua API

| 接口 | 说明 |
|---|---|
| `FSNotify.RegisterProtocol()` | 注册 `fsnotify` 协议到当前 service, 每个订阅者启动时调用一次 |
| `FSNotify.RegisterHandler(watchPath, eventsMask, handler)` | 订阅 `watchPath` 上 `eventsMask` 指定的事件, `handler(watchPath, ev)` 回调 |
| `FSNotify.UnregisterHandler(watchPath)` | 取消对 `watchPath` 的订阅 |
| `FSNotify.UnregisterAllHandlers()` | 取消当前 service 的全部订阅 |

事件 mask 常量从 `Lfsnotify.EventMask` 读取, 订阅位: `IN_ATTRIB`, `IN_CREATE`,
`IN_MODIFY`, `IN_DELETE`, `IN_DELETE_SELF`, `IN_MOVE_SELF`, `IN_MOVED_FROM`,
`IN_MOVED_TO`; 返回属性位: `IN_ISDIR`, `IN_IGNORED` (仅用于判断 `ev.mask`,
不要放进订阅 mask).

回调参数:
* `watchPath` - 订阅时传入的路径字符串
* `ev.wd` / `ev.mask` / `ev.cookie` / `ev.name` - 对应 `struct inotify_event` 字段

注意:
* `watchPath` 不能包含空格
* `eventsMask` 为 0 或含非订阅位时, 会被 fsnotify 服务拒绝
* 同一 service 对同一 `watchPath` 重复 RegisterHandler 会**覆盖**上一次的
  handler 和 mask

## C API
业务 service 也可直接以 `PTYPE_FSNOTIFY` 消息协议与 fsnotify 服务交互,
命令格式 (文本, 空格分隔):
```
AddWatchPath <watch_path> <events_mask>
RmWatchPath <watch_path>
RmWatchService
```
事件回调 payload 布局 (二进制):
```
[ struct inotify_event (含柔性数组 name[]) ][ watch_path (无 '\0' 结尾) ]
└── 长度 sizeof(inotify_event) + ev->len ──┘└── 剩余长度 = sz - data_sz ─┘
```

## 编译 && 运行示例
* 将编译后的 skynet 仓库连接到工程目录下
```bash
ln -sf $YOUR_SKYNET_PATH skynet
```
* 编译相关动态库
```bash
make clean && make
```
* 启动 lua api 测试进程 (三个 watcher 订阅同一目录, 各自使用不同 mask 验证过滤)
```bash
./skynet/skynet example/fsnotify/config.testlua
```
* 启动 c++ api 测试进程 (master + agent, agent 通过 skynet timer 延迟触发文件操作)
```bash
./skynet/skynet example/fsnotify/config.testcc
```
* 在 Lua 测试跑起来后, 也可手动触发验证
```bash
# lua 测试默认监听 /tmp/skynet_fsnotify_lua_test/, 已由 svc 3 内部做 CREATE/MODIFY/DELETE
# 观察终端日志, 应看到 svc 1 (IN_CREATE) / svc 2 (IN_DELETE) / svc 3 (全量) 各自接收到对应事件
```
