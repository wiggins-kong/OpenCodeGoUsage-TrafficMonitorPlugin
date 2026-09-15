// UsageItem.h —— 一个显示条目
//
// 7 个条目共用这一个类，靠构造参数区分（哪个窗口、是要"已用"还是"剩余"、还是倒计时）。
#pragma once

#include "TmSdk.h"
#include "UsageService.h"

#include <mutex>
#include <string>

namespace ocg
{
    class CUsageItem : public IPluginItem
    {
    public:
        enum Kind
        {
            KIND_USED,          // 已用百分比
            KIND_LEFT,          // 剩余百分比（100 - 已用）
            KIND_COUNTDOWN,     // 距离最近一次重置的倒计时
        };

        CUsageItem(const wchar_t* id, const wchar_t* name, const wchar_t* label,
                   const wchar_t* sample, int window_index, Kind kind);

        // ---- IPluginItem ----
        const wchar_t* GetItemName() const override { return m_name; }
        const wchar_t* GetItemId() const override { return m_id; }
        const wchar_t* GetItemLableText() const override { return m_label; }
        const wchar_t* GetItemValueText() const override;
        const wchar_t* GetItemValueSampleText() const override { return m_sample; }
        int OnMouseEvent(MouseEventType type, int x, int y, void* hWnd, int flag) override;

        // 由 ITMPlugin::DataRequired 调用（监控线程），格式化好文本缓存起来
        void UpdateFrom(const UsageSnapshot& snap);

    private:
        const wchar_t* m_id;
        const wchar_t* m_name;
        const wchar_t* m_label;
        const wchar_t* m_sample;
        int m_window;
        Kind m_kind;

        mutable std::mutex m_mutex;
        std::wstring m_value;       // UpdateFrom 写
        mutable std::wstring m_return;  // GetItemValueText 的稳定返回缓冲
    };
}
