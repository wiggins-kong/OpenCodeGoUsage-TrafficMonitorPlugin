#include "OptionsDlg.h"
#include "Config.h"
#include "PluginGlobals.h"
#include "UsageParse.h"
#include "UsageService.h"

#include "../res/resource.h"

namespace ocg
{
    namespace
    {
        struct DlgContext
        {
            PluginSettings settings;
        };

        std::wstring GetDlgText(HWND hDlg, int id)
        {
            HWND h = ::GetDlgItem(hDlg, id);
            if (h == nullptr)
                return std::wstring();
            const int len = ::GetWindowTextLengthW(h);
            std::wstring s((size_t)len + 1, L'\0');
            const int n = ::GetWindowTextW(h, &s[0], len + 1);
            s.resize((n > 0) ? (size_t)n : 0);
            return tmu::Trim(s);
        }

        void SetDlgText(HWND hDlg, int id, const std::wstring& text)
        {
            ::SetDlgItemTextW(hDlg, id, text.c_str());
        }

        std::wstring FormatWindowLine(int index, const UsageWindow& w)
        {
            SYSTEMTIME now{};
            ::GetLocalTime(&now);

            std::wstring line;
            switch (index)
            {
            case WIN_ROLLING: line = L"5h："; break;
            case WIN_WEEKLY:  line = L"周："; break;
            default:          line = L"月："; break;
            }

            if (!w.found)
            {
                line += L"响应里没有这个窗口";
                return line;
            }

            line += L"已用 " + std::to_wstring(w.percent) + L"% / 剩余 " + std::to_wstring(100 - w.percent) + L"%";
            if (!w.resets_at.empty())
            {
                SYSTEMTIME local{};
                if (tmu::Iso8601ToLocal(w.resets_at, local))
                {
                    std::wstring when;
                    if (index == WIN_ROLLING)
                        when = tmu::FormatClock(local);
                    else if (index == WIN_WEEKLY)
                        when = std::wstring(tmu::WeekdayName(local.wDayOfWeek)) + L" " + tmu::FormatClock(local);
                    else
                        when = tmu::FormatDateClock(local);

                    line += L"，重置 " + when + L"（" + tmu::FormatCountdown(tmu::SecondsBetween(now, local)) + L" 后）";
                }
            }
            if (w.status == "rate-limited")
                line += L"  [已限流]";
            return line;
        }

        void DoTestConnection(HWND hDlg)
        {
            const std::wstring key = GetDlgText(hDlg, IDC_EDIT_KEY);

            if (key.empty())
            {
                SetDlgText(hDlg, IDC_EDIT_RESULT,
                           L"输入框为空：请填入 sk- 开头的 Key（在 opencode.ai/auth 登录后转到「API 密钥」页面复制）。");
                return;
            }

            SetDlgText(hDlg, IDC_EDIT_RESULT, L"正在请求 https://opencode.ai/zen/go/v1/usage …（最多等 10 秒）");
            ::UpdateWindow(hDlg);

            const std::wstring proxy = GetDlgText(hDlg, IDC_EDIT_PROXY);
            UsageResult parsed;
            std::wstring err;
            std::wstring proxyUsed;
            const bool ok = UsageService::FetchNow(key, proxy, parsed, err, proxyUsed);

            std::wstring out;
            if (ok)
            {
                out = L"成功（HTTP 200）\r\n";
                for (int i = 0; i < WIN_COUNT; ++i)
                {
                    out += FormatWindowLine(i, parsed.windows[i]);
                    out += L"\r\n";
                }
                out += L"代理：" + proxyUsed;
            }
            else
            {
                out = L"失败：" + err + L"\r\n代理：" + proxyUsed;
            }
            out += L"\r\nKey 来源：输入框";
            SetDlgText(hDlg, IDC_EDIT_RESULT, out);
        }

        INT_PTR CALLBACK OptionsProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam)
        {
            DlgContext* ctx = reinterpret_cast<DlgContext*>(::GetWindowLongPtrW(hDlg, GWLP_USERDATA));

            switch (msg)
            {
            case WM_INITDIALOG:
            {
                ctx = reinterpret_cast<DlgContext*>(lParam);
                ::SetWindowLongPtrW(hDlg, GWLP_USERDATA, (LONG_PTR)ctx);

                // 所有可见文案都在这里设，不放进 .rc —— 避免 rc.exe 处理中文代码页的差异
                ::SetWindowTextW(hDlg, L"OpenCode Go 用量 — 插件选项");
                SetDlgText(hDlg, IDC_STATIC_KEY, L"API Key（sk- 开头）：");
                SetDlgText(hDlg, IDC_STATIC_INTERVAL, L"刷新间隔（秒）：");
                SetDlgText(hDlg, IDC_STATIC_INTERVAL_RANGE, L"30–3600");
                SetDlgText(hDlg, IDC_STATIC_PROXY, L"代理（留空 = 自动）：");
                SetDlgText(hDlg, IDC_STATIC_PROXY_HINT, L"direct = 直连；也可填 host:port");
                SetDlgText(hDlg, IDC_BTN_TEST, L"测试连接");
                ::SetDlgItemTextW(hDlg, IDOK, L"确定");
                ::SetDlgItemTextW(hDlg, IDCANCEL, L"取消");

                SetDlgText(hDlg, IDC_STATIC_HINT2,
                           L"Key 在 opencode.ai/auth 登录后的「API 密钥」页面创建；"
                           L"「测试连接」只验证输入框里当前的值，不会保存。");
                SetDlgText(hDlg, IDC_EDIT_RESULT, L"点「测试连接」立刻验证 Key 和网络。");

                // 已保存的 Key 不回显明文：ES_PASSWORD 在 .rc 里（运行时改窗口样式对已建好的
                // edit 控件不生效），这里只把遮罩字符换成圆点；GetDlgText 仍能取到真实值。
                const HWND hKeyEdit = ::GetDlgItem(hDlg, IDC_EDIT_KEY);
                ::SendMessageW(hKeyEdit, EM_SETPASSWORDCHAR, (WPARAM)L'\u25CF', 0);
                ::InvalidateRect(hKeyEdit, nullptr, TRUE);
                ::SetDlgItemTextW(hDlg, IDC_EDIT_KEY, ctx->settings.api_key.c_str());
                ::SetDlgItemTextW(hDlg, IDC_EDIT_INTERVAL, std::to_wstring(ctx->settings.interval_sec).c_str());
                ::SetDlgItemTextW(hDlg, IDC_EDIT_PROXY, ctx->settings.proxy.c_str());

                SetDlgText(hDlg, IDC_STATIC_KEY_SOURCE,
                           ctx->settings.api_key.empty()
                               ? L"尚未保存 Key"
                               : L"已保存 Key（输入框以圆点显示；输入新值会覆盖，清空后保存则删除）");
                return TRUE;
            }

            case WM_COMMAND:
                switch (LOWORD(wParam))
                {
                case IDOK:
                {
                    const std::wstring intervalText = GetDlgText(hDlg, IDC_EDIT_INTERVAL);
                    int interval = 60;
                    if (!intervalText.empty())
                    {
                        wchar_t* end = nullptr;
                        const long v = ::wcstol(intervalText.c_str(), &end, 10);
                        if (end == intervalText.c_str() || v <= 0)
                        {
                            ::MessageBoxW(hDlg, L"刷新间隔请输入 30 到 3600 之间的整数。",
                                          L"OpenCode Go 用量", MB_OK | MB_ICONWARNING);
                            ::SetFocus(::GetDlgItem(hDlg, IDC_EDIT_INTERVAL));
                            return TRUE;
                        }
                        interval = (int)v;
                        if (interval < 30 || interval > 3600)
                        {
                            ::MessageBoxW(hDlg, L"刷新间隔超出范围，已按 30–3600 夹取。",
                                          L"OpenCode Go 用量", MB_OK | MB_ICONINFORMATION);
                        }
                    }

                    PluginSettings s;
                    s.api_key = GetDlgText(hDlg, IDC_EDIT_KEY);
                    s.interval_sec = Config::ClampInterval(interval);
                    s.proxy = GetDlgText(hDlg, IDC_EDIT_PROXY);

                    ctx->settings = s;
                    Config::Instance().Set(s);
                    Config::Instance().Save();                  // 插件对象永远不会被析构，必须立刻落盘
                    UsageService::Instance().OnSettingsChanged();
                    ::EndDialog(hDlg, IDOK);
                    return TRUE;
                }

                case IDCANCEL:
                    ::EndDialog(hDlg, IDCANCEL);
                    return TRUE;

                case IDC_BTN_TEST:
                    DoTestConnection(hDlg);
                    return TRUE;

                default:
                    break;
                }
                break;

            default:
                break;
            }
            return FALSE;
        }
    }

    ITMPlugin::OptionReturn ShowPluginOptions(HWND hParent)
    {
        DlgContext ctx;
        ctx.settings = Config::Instance().Get();

        const INT_PTR ret = ::DialogBoxParamW(g_hModule, MAKEINTRESOURCEW(IDD_OPTIONS),
                                              hParent, &OptionsProc, (LPARAM)&ctx);
        if (ret == IDOK)
            return ITMPlugin::OR_OPTION_CHANGED;
        return ITMPlugin::OR_OPTION_UNCHANGED;
    }
}
