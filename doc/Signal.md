## Service信号捕获

## 实现原理
![flowchart](https://github.com/xingshuo/skynet-ext/blob/main/doc/SignalArch.png)

## 编译 && 运行示例
* 将编译后的skynet仓库连接到工程目录下
```bash
ln -sf $YOUR_SKYNET_PATH skynet
```
* 编译相关动态库
```bash
make clean && make
```
* 启动lua api测试进程
```bash
./skynet/skynet example/signal/config.testlua
```
* 启动c++ api测试进程
```bash
./skynet/skynet example/signal/config.testcc
```
* 运行测试指令
```bash
kill -s SIGUSR1 $skynet_process_id
# 观察终端输出
kill -s SIGUSR2 $skynet_process_id
# 观察终端输出
kill -s SIGUSR1 $skynet_process_id
# 观察进程是否退出
```