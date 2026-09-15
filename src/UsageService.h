// UsageService.h —— 后台轮询 opencode 的用量接口，并缓存最近一次结果
//
// 为什么要有后台线程：宿主的 DataRequired() 是在监控线程上每秒调一次的，在里面发 HTTP
// 会把整个任务栏卡住。这里所有网络动作都在自己的线程里，DataRequired 只读一份快照。
#pragma once

#include "TmCommon.h"
#include "UsageParse.h"

#include <atomic>
#include <mutex>
#include <string>

namespace ocg
{
    struct WindowState
    {
        bool found = false;
        bool rate_limited = false;      // status == "rate-limited"
        int used = 0;                   // 已用百分比 0..100
        bool have_reset = false;
        SYSTEMTIME reset_local{};       // 重置时刻（已转本地时间）
    };

    struct UsageSnapshot
    {
        bool ever_ok = false;           // 至少成功过一次
        bool last_ok = false;           // 最近一次是否成功
        std::wstring error;             // 最近一次失败的说明（成功后清空）
        std::wstring last_update;       // 最近一次成功的时间 HH:mm:ss
        std::wstring key_source;        // 当前生效的 Key 来源（每次抓取时更新）
        std::wstring proxy_used;        // 实际走的代理（用于排查）
        WindowState win[WIN_COUNT];
    };

    class UsageService
    {
    public:
        static UsageService& Instance();

        void Start();                   // 幂等：首次调用时创建后台线程
        void RequestRefresh();          // 立即刷新
        void OnSettingsChanged();       // 配置变了：唤醒线程立刻用新配置拉一次
        UsageSnapshot Snapshot() const;

        // 同步抓一次（选项对话框的「测试连接」用）。失败时 err 里是面向用户的说明。
        static bool FetchNow(const std::wstring& key, const std::wstring& proxy_setting,
                             UsageResult& parsed, std::wstring& err, std::wstring& proxy_used);

    private:
        UsageService() = default;
        UsageService(const UsageService&) = delete;
        UsageService& operator=(const UsageService&) = delete;

        static DWORD WINAPI ThreadProc(LPVOID param);
        void WorkerLoop();
        void DoFetch();
        void ApplySuccess(const UsageResult& parsed, const std::wstring& proxy_used);
        void ApplyFailure(const std::wstring& message, const std::wstring& proxy_used);

        HANDLE m_thread = nullptr;
        HANDLE m_wake = nullptr;
        std::atomic<bool> m_started{ false };
        mutable std::mutex m_mutex;
        UsageSnapshot m_snap;
    };

    // 把配置里的代理设置描述成人话（对话框和 tooltip 都用）
    std::wstring DescribeProxySetting(const std::wstring& setting);
}
