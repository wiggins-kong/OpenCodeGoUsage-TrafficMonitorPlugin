// TmSdk.h —— 包一层宿主的接口头
//
// 直接 include PluginInterface.h 会把上游那些空实现的默认参数刷成 C4100 警告
// （文件是从 TrafficMonitor 仓库原样拷贝的，不改它）。这里就地静音，只影响这一处。
#pragma once

#pragma warning(push)
#pragma warning(disable : 4100)     // unreferenced formal parameter
#include "../include/PluginInterface.h"
#pragma warning(pop)
