// UsageParse.h —— 把 /zen/go/v1/usage 的响应体解析成结构体（纯 C++，不依赖 Windows）
//
// 响应的两种形态（2026-09 实测）：
//   200: {"usage":{"rolling":{"status":"ok","percent":1,"resetsAt":"2026-08-15T12:34:47.397Z"},
//                  "weekly":{...},"monthly":{...}}}
//   4xx: {"type":"error","error":{"type":"AuthError","message":"Unauthorized"}}
// percent 是"已用"百分比（整数），余量需要自己用 100 减。
#pragma once

#include <string>

namespace ocg
{
    // 三个窗口。接口里的名字是 rolling/weekly/monthly，rolling 就是文档里的 5 小时窗口。
    enum WindowIndex
    {
        WIN_ROLLING = 0,    // 5 小时
        WIN_WEEKLY,         // 每周
        WIN_MONTHLY,        // 每月
        WIN_COUNT
    };

    struct UsageWindow
    {
        bool found = false;         // 该窗口是否出现在响应里
        std::string status;         // "ok" / "rate-limited"
        int percent = 0;            // 已用百分比
        std::string resets_at;      // ISO8601 UTC，原样保留，由上层转成本地时间
    };

    struct UsageResult
    {
        bool is_error = false;              // 响应是错误体（或 HTTP 状态非 200）
        int http_status = 0;
        std::string error_type;             // AuthError / EntitlementError / ...
        std::string error_message;          // 面向用户的说明（来自服务端 message）
        UsageWindow windows[WIN_COUNT];
    };

    // 解析响应体。返回 false 表示连错误信息都读不出来（比如 HTML 错误页），
    // 此时 out.error_message 里放的是本地生成的说明。
    bool ParseUsage(const std::string& body, int http_status, UsageResult& out);

    // 窗口名（英文键名），用于日志/工具提示
    const char* WindowKeyName(int index);
}
