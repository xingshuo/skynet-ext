#pragma once
#include <stdint.h>
#include "ms-timer/common.h"

namespace skynet_ext {
namespace ms_timer {
	/// @brief 初始化mpoller
	/// @param nthreads 启动poller数, 每个poller绑定一个独立线程, Notice: nthreads为0会强制设置为1
	/// @return 0 成功 <0 失败, 详见: ErrCode
	int InitPoller(uint32_t nthreads);

	/// @brief 释放mpoller
	void ExitPoller();

	/// @brief 创建定时器
	/// @param service_handle 定时器触发回调服务地址
	/// @param session 定时器唯一标识, <service_handle, session>唯一
	/// @param count 定时器触发次数: count <= 0永久, count > 0有限次
	/// @param interval_ms 定时器触发间隔: > 0
	/// @return 0 成功 <0 失败, 详见: ErrCode
	int StartTimer(uint32_t service_handle, int session, int count, uint32_t interval_ms);

	/// @brief 销毁定时器
	/// @param service_handle 定时器触发回调服务地址
	/// @param session 定时器唯一标识, <service_handle, session>唯一
	void StopTimer(uint32_t service_handle, int session);
} // namespace ms_timer
} // namespace skynet_ext