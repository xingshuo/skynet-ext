## 毫秒级定时器

## 平台支持
```
目前只支持linux
```

## 实现原理
* 基于linux平台提供的timerfd机制
```text
通过启动参数控制创建多个poller, 每个poller绑定一个线程并通过epoll监听pipe的read fd和timer fd
可读事件通知时, 更新定时器节点组成的PriorityQueue, 分发定时器事件并重新设置timer fd的timeout参数
```
![flowchart](https://github.com/xingshuo/skynet-ext/blob/main/doc/MSTimerArch.png)

## 注意事项
* 由于skynet框架消息调度机制，如需确保毫秒级精度，建议将[此处usleep间隔](https://github.com/cloudwu/skynet/blob/master/skynet-src/skynet_start.c#L137)调整为更小值，如:500(0.5ms)

## 编译 && 运行示例
* 将编译后的skynet仓库连接到工程目录下
```bash
ln -sf $YOUR_SKYNET_PATH skynet
```
* 编译相关动态库
```bash
make clean && make
```
* 运行lua api测试用例
```bash
./skynet/skynet example/ms-timer/config.testlua
```
* 运行c++ api测试用例
```bash
./skynet/skynet example/ms-timer/config.testcc
```