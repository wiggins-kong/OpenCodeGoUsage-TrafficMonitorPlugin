#include "Plugin.h"
#include "Config.h"
#include "OptionsDlg.h"
#include "PluginGlobals.h"

#include <shellapi.h>

#pragma comment(lib, "shell32.lib")

HMODULE g_hModule = nullptr;

namespace
{
    const wchar_t* const kConsoleUrl = L"https://opencode.ai/console";
    const wchar_t* const kWindowNames[ocg::WIN_COUNT] = { L"5h", L"周", L"月" };

    const wchar_t* const kCmdRefresh = L"立即刷新";
    const wchar_t* const kCmdOpenConsole = L"打开 OpenCode 控制台";
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID /*reserved*/)
{
    if (reason == DLL_PROCESS_ATTACH)
    {
        g_hModule = hModule;
        ::DisableThreadLibraryCalls(hModule);
    }
    return TRUE;
}

COpenCodeGoPlugin& COpenCodeGoPlugin::Instance()
{
    // 全局静态对象：宿主持有它的指针且在进程结束前不会释放它
    static COpenCodeGoPlugin instance;
    return instance;
}

COpenCodeGoPlugin::COpenCodeGoPlugin()
{
    using ocg::CUsageItem;
    auto add = [this](const wchar_t* id, const wchar_t* name, const wchar_t* label,
                      const wchar_t* sample, int window, CUsageItem::Kind kind)
    {
        m_items.emplace_back(new CUsageItem(id, name, label, sample, window, kind));
    };

    add(L"ocg5hused", L"5小时已用", L"5h",   L"100%",   ocg::WIN_ROLLING, CUsageItem::KIND_USED);
    add(L"ocg5hleft", L"5小时剩余", L"5h余", L"100%",   ocg::WIN_ROLLING, CUsageItem::KIND_LEFT);
    add(L"ocgwused",  L"本周已用",  L"周",   L"100%",   ocg::WIN_WEEKLY,  CUsageItem::KIND_USED);
    add(L"ocgwleft",  L"本周剩余",  L"周余", L"100%",   ocg::WIN_WEEKLY,  CUsageItem::KIND_LEFT);
    add(L"ocgmused",  L"本月已用",  L"月",   L"100%",   ocg::WIN_MONTHLY, CUsageItem::KIND_USED);
    add(L"ocgmleft",  L"本月剩余",  L"月余", L"100%",   ocg::WIN_MONTHLY, CUsageItem::KIND_LEFT);
    add(L"ocgreset",  L"重置倒计时", L"重置", L"23h59m", ocg::WIN_ROLLING, CUsageItem::KIND_COUNTDOWN);
}

IPluginItem* COpenCodeGoPlugin::GetItem(int index)
{
    if (index < 0 || (size_t)index >= m_items.size())
        return nullptr;
    return m_items[(size_t)index].get();
}

void COpenCodeGoPlugin::EnsureInit()
{
    if (m_inited)
        return;
    m_inited = true;

    // 正常时序：宿主先发 EI_CONFIG_DIR 再调用 DataRequired，所以这里通常已经有目录了。
    // 万一没有（比如插件被别的东西提前调用），退回 DLL 自己所在的目录。
    if (ocg::Config::Instance().IniDir().empty())
    {
        std::wstring path = tmu::GetModulePath(g_hModule);
        const size_t p = path.find_last_of(L"\\/");
        if (p != std::wstring::npos)
            path = path.substr(0, p + 1);
        ocg::Config::Instance().Load(path);
    }

    ocg::UsageService::Instance().Start();
}

void COpenCodeGoPlugin::DataRequired()
{
    EnsureInit();

    const ocg::UsageSnapshot snap = ocg::UsageService::Instance().Snapshot();
    for (const auto& item : m_items)
        item->UpdateFrom(snap);
    BuildTooltip(snap);
}

void COpenCodeGoPlugin::BuildTooltip(const ocg::UsageSnapshot& snap)
{
    SYSTEMTIME now{};
    ::GetLocalTime(&now);

    std::wstring t = L"OpenCode Go 用量";
    for (int i = 0; i < ocg::WIN_COUNT; ++i)
    {
        const ocg::WindowState& w = snap.win[i];
        t += L"\r\n";
        t += kWindowNames[i];
        t += L"：";

        if (!snap.ever_ok || !w.found)
        {
            t += L"--";
            continue;
        }

        t += L"已用 " + std::to_wstring(w.used) + L"% / 剩余 " + std::to_wstring(100 - w.used) + L"%";
        if (w.have_reset)
        {
            std::wstring when;
            if (i == ocg::WIN_ROLLING)
                when = tmu::FormatClock(w.reset_local);
            else if (i == ocg::WIN_WEEKLY)
                when = std::wstring(tmu::WeekdayName(w.reset_local.wDayOfWeek)) + L" " + tmu::FormatClock(w.reset_local);
            else
                when = tmu::FormatDateClock(w.reset_local);

            t += L"，重置 " + when + L"（" + tmu::FormatCountdown(tmu::SecondsBetween(now, w.reset_local)) + L" 后）";
        }
        if (w.rate_limited)
            t += L"  [已限流]";
    }

    if (!snap.key_source.empty())
        t += L"\r\nKey：" + snap.key_source;
    if (!snap.proxy_used.empty())
        t += L"\r\n代理：" + snap.proxy_used;
    if (snap.ever_ok && !snap.last_update.empty())
        t += L"\r\n更新：" + snap.last_update;
    if (!snap.error.empty())
        t += L"\r\n⚠ " + snap.error;

    std::lock_guard<std::mutex> lock(m_mutex);
    m_tooltip = t;
}

const wchar_t* COpenCodeGoPlugin::GetTooltipInfo()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_return_tooltip = m_tooltip;
    return m_return_tooltip.c_str();
}

ITMPlugin::OptionReturn COpenCodeGoPlugin::ShowOptionsDialog(void* hParent)
{
    EnsureInit();
    return ocg::ShowPluginOptions(static_cast<HWND>(hParent));
}

const wchar_t* COpenCodeGoPlugin::GetInfo(PluginInfoIndex index)
{
    // 这些字符串必须是静态的、且永远不能返回 nullptr
    switch (index)
    {
    case TMI_NAME:        return L"OpenCode Go 用量";
    case TMI_DESCRIPTION: return L"显示 OpenCode Go 订阅的 5 小时 / 每周 / 每月用量与余量。数据来自 opencode.ai 的用量接口，需要在插件选项里填一个 Go 的 API Key（sk- 开头）。";
    case TMI_AUTHOR:      return L"wiggins";
    case TMI_COPYRIGHT:   return L"";
    case TMI_VERSION:     return L"1.0";
    case TMI_URL:         return L"";
    default:              return L"";
    }
}

void COpenCodeGoPlugin::OnExtenedInfo(ExtendedInfoIndex index, const wchar_t* data)
{
    if (index == EI_CONFIG_DIR && data != nullptr)
    {
        ocg::Config::Instance().Load(data);
        ocg::UsageService::Instance().OnSettingsChanged();
    }
}

void COpenCodeGoPlugin::OnInitialize(ITrafficMonitor* pApp)
{
    m_app = pApp;
}

int COpenCodeGoPlugin::GetCommandCount()
{
    return 2;
}

const wchar_t* COpenCodeGoPlugin::GetCommandName(int command_index)
{
    switch (command_index)
    {
    case 0: return kCmdRefresh;
    case 1: return kCmdOpenConsole;
    default: return nullptr;
    }
}

void COpenCodeGoPlugin::OnPluginCommand(int command_index, void* /*hWnd*/, void* /*para*/)
{
    switch (command_index)
    {
    case 0:
        ocg::UsageService::Instance().RequestRefresh();
        break;
    case 1:
        ::ShellExecuteW(nullptr, L"open", kConsoleUrl, nullptr, nullptr, SW_SHOWNORMAL);
        break;
    default:
        break;
    }
}

// 宿主按这个名字查找导出函数（GetProcAddress("TMPluginGetInstance")），必须是 extern "C" 且不被修饰
extern "C" __declspec(dllexport) ITMPlugin* TMPluginGetInstance()
{
    return &COpenCodeGoPlugin::Instance();
}
