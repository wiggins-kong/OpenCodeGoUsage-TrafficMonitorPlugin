#include "UsageService.h"
#include "Config.h"

#include <winhttp.h>

#pragma comment(lib, "winhttp.lib")

namespace ocg
{
    namespace
    {
        const wchar_t* const kHost = L"opencode.ai";
        const wchar_t* const kPath = L"/zen/go/v1/usage";
        const wchar_t* const kUserAgent = L"TrafficMonitor-OpenCodeGoUsage/1.0";
        const DWORD kTimeoutMs = 10000;
        const size_t kMaxBodyBytes = 1u << 20;      // 防御：响应体不该超过 1MB

        struct HttpOutcome
        {
            bool transport_ok = false;
            int status = 0;
            std::string body;
            DWORD win_err = 0;
        };

        // WinHTTP 四个句柄的 RAII，保证任何错误分支下都不会泄漏
        struct WinHttpHandles
        {
            HINTERNET session = nullptr;
            HINTERNET connect = nullptr;
            HINTERNET request = nullptr;

            ~WinHttpHandles()
            {
                if (request) ::WinHttpCloseHandle(request);
                if (connect) ::WinHttpCloseHandle(connect);
                if (session) ::WinHttpCloseHandle(session);
            }
        };

        std::wstring WinHttpErrorText(DWORD e)
        {
            switch (e)
            {
            case ERROR_WINHTTP_NAME_NOT_RESOLVED:   return L"DNS 解析失败（检查网络或代理）";
            case ERROR_WINHTTP_CANNOT_CONNECT:      return L"无法连接 opencode.ai（可能需要代理）";
            case ERROR_WINHTTP_TIMEOUT:             return L"请求超时";
            case ERROR_WINHTTP_SECURE_FAILURE:      return L"TLS 握手/证书校验失败";
            case ERROR_WINHTTP_CONNECTION_ERROR:    return L"连接被中断";
            case ERROR_WINHTTP_INVALID_SERVER_RESPONSE: return L"服务器返回了无效响应";
            case ERROR_WINHTTP_OPERATION_CANCELLED: return L"请求被取消";
            default:                                return L"网络错误（WinHTTP " + std::to_wstring(e) + L"）";
            }
        }

        // "http=127.0.0.1:7890;https=127.0.0.1:7891" -> 取 https 项，没有就取 http 项，再没有就整体
        std::wstring PickProxyFromServerList(const std::wstring& raw)
        {
            std::wstring https;
            std::wstring http;
            size_t start = 0;
            while (start <= raw.size())
            {
                size_t end = raw.find(L';', start);
                if (end == std::wstring::npos)
                    end = raw.size();
                std::wstring item = tmu::Trim(raw.substr(start, end - start));
                const size_t eq = item.find(L'=');
                if (eq != std::wstring::npos)
                {
                    const std::wstring scheme = tmu::Trim(item.substr(0, eq));
                    const std::wstring value = tmu::Trim(item.substr(eq + 1));
                    if (_wcsicmp(scheme.c_str(), L"https") == 0 && !value.empty())
                        https = value;
                    else if (_wcsicmp(scheme.c_str(), L"http") == 0 && !value.empty())
                        http = value;
                }
                else if (!item.empty())
                {
                    https = item;   // 形如 "127.0.0.1:7890"
                }
                if (end == raw.size())
                    break;
                start = end + 1;
            }
            if (!https.empty())
                return https;
            return http;
        }

        std::wstring StripScheme(const std::wstring& url)
        {
            std::wstring s = tmu::Trim(url);
            const size_t pos = s.find(L"://");
            if (pos != std::wstring::npos)
                s = s.substr(pos + 3);
            while (!s.empty() && s.back() == L'/')
                s.pop_back();
            return s;
        }

        struct ProxyPlan
        {
            bool use_proxy = false;
            std::wstring server;
            std::wstring desc;
        };

        ProxyPlan ResolveProxy(const std::wstring& setting)
        {
            ProxyPlan plan;

            if (_wcsicmp(setting.c_str(), L"direct") == 0)
            {
                plan.desc = L"直连（手动设置为 direct）";
                return plan;
            }
            if (!setting.empty())
            {
                plan.use_proxy = true;
                plan.server = setting;
                plan.desc = L"手动代理 " + setting;
                return plan;
            }

            // 环境变量（Git Bash / curl / 各种代理工具常写这个）
            static const wchar_t* const kEnvNames[] = { L"HTTPS_PROXY", L"https_proxy", L"HTTP_PROXY", L"http_proxy" };
            for (const wchar_t* name : kEnvNames)
            {
                wchar_t buf[1024]{};
                const DWORD n = ::GetEnvironmentVariableW(name, buf, _countof(buf));
                if (n == 0 || n >= _countof(buf))
                    continue;
                const std::wstring server = StripScheme(buf);
                if (server.empty())
                    continue;
                plan.use_proxy = true;
                plan.server = server;
                plan.desc = std::wstring(L"环境变量 ") + name + L"（" + server + L"）";
                return plan;
            }

            // 系统代理（Internet 选项；Clash 之类的"系统代理"模式写在这里）
            HKEY hKey = nullptr;
            if (::RegOpenKeyExW(HKEY_CURRENT_USER,
                                L"Software\\Microsoft\\Windows\\CurrentVersion\\Internet Settings",
                                0, KEY_READ, &hKey) == ERROR_SUCCESS)
            {
                DWORD enable = 0;
                DWORD cbEnable = sizeof(enable);
                DWORD type = 0;
                if (::RegQueryValueExW(hKey, L"ProxyEnable", nullptr, &type, reinterpret_cast<LPBYTE>(&enable), &cbEnable) == ERROR_SUCCESS
                    && type == REG_DWORD && enable != 0)
                {
                    wchar_t server[2048]{};
                    DWORD cbServer = sizeof(server);
                    if (::RegQueryValueExW(hKey, L"ProxyServer", nullptr, &type, reinterpret_cast<LPBYTE>(server), &cbServer) == ERROR_SUCCESS
                        && type == REG_SZ)
                    {
                        const std::wstring picked = PickProxyFromServerList(server);
                        if (!picked.empty())
                        {
                            plan.use_proxy = true;
                            plan.server = picked;
                            plan.desc = L"系统代理 " + picked;
                        }
                    }
                }
                ::RegCloseKey(hKey);
            }

            if (!plan.use_proxy)
                plan.desc = L"直连（未检测到代理）";
            return plan;
        }

        bool HttpGetUsage(const std::wstring& key, const std::wstring& proxy, bool use_proxy, HttpOutcome& out)
        {
            out = HttpOutcome();

            WinHttpHandles h;
            h.session = ::WinHttpOpen(kUserAgent,
                                      use_proxy ? WINHTTP_ACCESS_TYPE_NAMED_PROXY : WINHTTP_ACCESS_TYPE_NO_PROXY,
                                      use_proxy ? proxy.c_str() : WINHTTP_NO_PROXY_NAME,
                                      WINHTTP_NO_PROXY_BYPASS, 0);
            if (!h.session)
            {
                out.win_err = ::GetLastError();
                return false;
            }
            ::WinHttpSetTimeouts(h.session, (int)kTimeoutMs, (int)kTimeoutMs, (int)kTimeoutMs, (int)kTimeoutMs);

            h.connect = ::WinHttpConnect(h.session, kHost, INTERNET_DEFAULT_HTTPS_PORT, 0);
            if (!h.connect)
            {
                out.win_err = ::GetLastError();
                return false;
            }

            h.request = ::WinHttpOpenRequest(h.connect, L"GET", kPath, nullptr,
                                             WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                                             WINHTTP_FLAG_SECURE);
            if (!h.request)
            {
                out.win_err = ::GetLastError();
                return false;
            }

            const std::wstring headers = L"Authorization: Bearer " + key + L"\r\nAccept: application/json\r\n";
            if (!::WinHttpAddRequestHeaders(h.request, headers.c_str(), (DWORD)-1L,
                                            WINHTTP_ADDREQ_FLAG_ADD | WINHTTP_ADDREQ_FLAG_REPLACE))
            {
                out.win_err = ::GetLastError();
                return false;
            }

            if (!::WinHttpSendRequest(h.request, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                                      WINHTTP_NO_REQUEST_DATA, 0, 0, 0))
            {
                out.win_err = ::GetLastError();
                return false;
            }

            if (!::WinHttpReceiveResponse(h.request, nullptr))
            {
                out.win_err = ::GetLastError();
                return false;
            }

            DWORD status = 0;
            DWORD cbStatus = sizeof(status);
            if (!::WinHttpQueryHeaders(h.request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                                       WINHTTP_HEADER_NAME_BY_INDEX, &status, &cbStatus, WINHTTP_NO_HEADER_INDEX))
            {
                out.win_err = ::GetLastError();
                return false;
            }
            out.status = (int)status;

            for (;;)
            {
                DWORD avail = 0;
                if (!::WinHttpQueryDataAvailable(h.request, &avail))
                {
                    out.win_err = ::GetLastError();
                    return false;
                }
                if (avail == 0)
                    break;
                if (out.body.size() + avail > kMaxBodyBytes)
                    break;

                std::string chunk(avail, '\0');
                DWORD read = 0;
                if (!::WinHttpReadData(h.request, &chunk[0], avail, &read))
                {
                    out.win_err = ::GetLastError();
                    return false;
                }
                chunk.resize(read);
                out.body += chunk;
            }

            out.transport_ok = true;
            return true;
        }
    }

    std::wstring DescribeProxySetting(const std::wstring& setting)
    {
        if (setting.empty())
            return L"自动（环境变量 → 系统代理 → 直连）";
        if (_wcsicmp(setting.c_str(), L"direct") == 0)
            return L"直连";
        return L"手动 " + setting;
    }

    UsageService& UsageService::Instance()
    {
        static UsageService instance;
        return instance;
    }

    void UsageService::Start()
    {
        bool expected = false;
        if (!m_started.compare_exchange_strong(expected, true))
            return;

        tmu::PinCurrentModule();     // 宿主退出时不会把我们卸掉
        m_wake = ::CreateEventW(nullptr, TRUE, FALSE, nullptr);
        m_thread = ::CreateThread(nullptr, 0, &UsageService::ThreadProc, this, 0, nullptr);
        if (m_thread)
            ::CloseHandle(m_thread);    // 线程自己会跑完进程生命周期
        else
            m_started = false;
    }

#pragma warning(suppress : 4702)    // WorkerLoop 是无限循环，下面的 return 到不了
    DWORD WINAPI UsageService::ThreadProc(LPVOID param)
    {
        static_cast<UsageService*>(param)->WorkerLoop();
        return 0;
    }

    void UsageService::WorkerLoop()
    {
        DoFetch();      // 启动后立刻拉一次，不用等第一个间隔

        for (;;)
        {
            const int interval = Config::Instance().Get().interval_sec;
            if (::WaitForSingleObject(m_wake, (DWORD)interval * 1000) == WAIT_OBJECT_0)
                ::ResetEvent(m_wake);
            DoFetch();
        }
    }

    void UsageService::RequestRefresh()
    {
        if (m_wake)
            ::SetEvent(m_wake);
    }

    void UsageService::OnSettingsChanged()
    {
        RequestRefresh();
    }

    void UsageService::DoFetch()
    {
        const KeyInfo ki = Config::Instance().ResolveKey();
        {
            // Key 来源只在这里刷新（ResolveKey 会读环境变量甚至 auth.json，不适合每帧调用）
            std::lock_guard<std::mutex> lock(m_mutex);
            m_snap.key_source = ki.source;
        }

        if (ki.key.empty())
        {
            ApplyFailure(L"未配置 API Key（右键插件条目 → 插件选项，或设置环境变量 OPENCODE_API_KEY）",
                         std::wstring());
            return;
        }

        const std::wstring proxy_setting = Config::Instance().Get().proxy;

        UsageResult parsed;
        std::wstring err;
        std::wstring proxy_used;
        if (!FetchNow(ki.key, proxy_setting, parsed, err, proxy_used))
        {
            // 失败时也要把代理写进快照：连不上时第一件要确认的就是走没走代理、走的哪个
            ApplyFailure(err, proxy_used);
            return;
        }

        ApplySuccess(parsed, proxy_used);
    }

    void UsageService::ApplySuccess(const UsageResult& parsed, const std::wstring& proxy_used)
    {
        SYSTEMTIME now{};
        ::GetLocalTime(&now);

        std::lock_guard<std::mutex> lock(m_mutex);
        for (int i = 0; i < WIN_COUNT; ++i)
        {
            const UsageWindow& src = parsed.windows[i];
            WindowState& dst = m_snap.win[i];
            dst.found = src.found;
            dst.rate_limited = (src.status == "rate-limited");

            int used = src.percent;
            if (used < 0)
                used = 0;
            if (used > 100)
                used = 100;
            dst.used = used;

            dst.have_reset = false;
            if (!src.resets_at.empty())
            {
                SYSTEMTIME local{};
                if (tmu::Iso8601ToLocal(src.resets_at, local))
                {
                    dst.have_reset = true;
                    dst.reset_local = local;
                }
            }
        }
        m_snap.ever_ok = true;
        m_snap.last_ok = true;
        m_snap.error.clear();
        m_snap.last_update = tmu::FormatClockSeconds(now);
        m_snap.proxy_used = proxy_used;
    }

    void UsageService::ApplyFailure(const std::wstring& message, const std::wstring& proxy_used)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_snap.last_ok = false;
        m_snap.error = message;
        if (!proxy_used.empty())
            m_snap.proxy_used = proxy_used;
        // 注意：这里刻意不动 win[]，保留上一次的数值（网络抖一下不该让任务栏变成 --）
    }

    UsageSnapshot UsageService::Snapshot() const
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_snap;
    }

    bool UsageService::FetchNow(const std::wstring& key, const std::wstring& proxy_setting,
                                UsageResult& parsed, std::wstring& err, std::wstring& proxy_used)
    {
        err.clear();
        proxy_used.clear();

        const ProxyPlan plan = ResolveProxy(proxy_setting);

        HttpOutcome resp;
        bool ok = HttpGetUsage(key, plan.server, plan.use_proxy, resp);
        proxy_used = plan.desc;

        // 代理可能已经不在了（代理软件关了但系统设置还留着），直连再试一次
        if (!ok && plan.use_proxy)
        {
            HttpOutcome direct;
            if (HttpGetUsage(key, std::wstring(), false, direct))
            {
                resp = direct;
                ok = true;
                proxy_used = plan.desc + L" 失败，已改用直连";
            }
        }

        if (!ok)
        {
            err = WinHttpErrorText(resp.win_err);
            return false;
        }

        ParseUsage(resp.body, resp.status, parsed);
        if (parsed.is_error)
        {
            std::wstring msg = std::to_wstring(parsed.http_status);
            if (!parsed.error_type.empty())
                msg += L" " + tmu::Utf8ToWide(parsed.error_type);
            const std::wstring detail = tmu::Utf8ToWide(parsed.error_message);
            if (!detail.empty())
                msg += L"：" + detail;

            if (parsed.http_status == 401)
                msg += L"（Key 无效或已被删除，去 opencode.ai/console 的 Keys 页面重新生成）";
            else if (parsed.http_status == 403)
                msg += L"（这个 Key 所在的工作区没有 OpenCode Go 订阅）";

            err = msg;
            return false;
        }

        return true;
    }
}
