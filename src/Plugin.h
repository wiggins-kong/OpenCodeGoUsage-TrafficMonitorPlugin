// Plugin.h —— 插件主体
#pragma once

#include "TmSdk.h"
#include "UsageItem.h"
#include "UsageService.h"

#include <memory>
#include <mutex>
#include <string>
#include <vector>

class COpenCodeGoPlugin : public ITMPlugin
{
public:
    static COpenCodeGoPlugin& Instance();

    // ---- ITMPlugin ----
    // GetAPIVersion() 刻意不重写：接口默认返回当前版本号，插件不该改它
    IPluginItem* GetItem(int index) override;
    void DataRequired() override;
    OptionReturn ShowOptionsDialog(void* hParent) override;
    const wchar_t* GetInfo(PluginInfoIndex index) override;
    const wchar_t* GetTooltipInfo() override;
    void OnExtenedInfo(ExtendedInfoIndex index, const wchar_t* data) override;
    void OnInitialize(ITrafficMonitor* pApp) override;
    int GetCommandCount() override;
    const wchar_t* GetCommandName(int command_index) override;
    void OnPluginCommand(int command_index, void* hWnd, void* para) override;

private:
    COpenCodeGoPlugin();
    COpenCodeGoPlugin(const COpenCodeGoPlugin&) = delete;
    COpenCodeGoPlugin& operator=(const COpenCodeGoPlugin&) = delete;

    void EnsureInit();
    void BuildTooltip(const ocg::UsageSnapshot& snap);

    std::vector<std::unique_ptr<ocg::CUsageItem>> m_items;

    mutable std::mutex m_mutex;          // 保护 m_tooltip
    std::wstring m_tooltip;              // DataRequired 写
    mutable std::wstring m_return_tooltip;   // GetTooltipInfo 的稳定返回缓冲

    bool m_inited = false;
    ITrafficMonitor* m_app = nullptr;    // 宿主接口（当前只在需要时保留）
};
