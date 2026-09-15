#include "UsageItem.h"

#include <climits>

namespace ocg
{
    CUsageItem::CUsageItem(const wchar_t* id, const wchar_t* name, const wchar_t* label,
                           const wchar_t* sample, int window_index, Kind kind)
        : m_id(id), m_name(name), m_label(label), m_sample(sample),
          m_window(window_index), m_kind(kind)
    {
        m_value = L"--";
    }

    const wchar_t* CUsageItem::GetItemValueText() const
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_return = m_value;
        return m_return.c_str();
    }

    int CUsageItem::OnMouseEvent(MouseEventType type, int /*x*/, int /*y*/, void* /*hWnd*/, int /*flag*/)
    {
        if (type == MT_LCLICKED)
        {
            // 点击条目 = 立即刷新。刻意返回 0（不吞掉事件）：任务栏那边宿主没有左键行为，
            // 但主窗口那边要靠它继续走拖拽逻辑，吞掉的话主窗口就拖不动了。
            UsageService::Instance().RequestRefresh();
        }
        return 0;
    }

    void CUsageItem::UpdateFrom(const UsageSnapshot& snap)
    {
        std::wstring text;
        bool has_value = false;

        if (m_kind == KIND_COUNTDOWN)
        {
            if (!snap.ever_ok)
            {
                text = L"--";
            }
            else
            {
                SYSTEMTIME now{};
                ::GetLocalTime(&now);

                bool any_positive = false;
                long long best_positive = LLONG_MAX;    // 最近的"还没到"的重置
                long long least_overdue = LLONG_MIN;    // 全都过期时取最接近的那个
                for (int i = 0; i < WIN_COUNT; ++i)
                {
                    if (!snap.win[i].have_reset)
                        continue;
                    has_value = true;
                    const long long d = tmu::SecondsBetween(now, snap.win[i].reset_local);
                    if (d > 0)
                    {
                        any_positive = true;
                        if (d < best_positive)
                            best_positive = d;
                    }
                    else if (d > least_overdue)
                    {
                        least_overdue = d;
                    }
                }

                if (any_positive)
                    text = tmu::FormatCountdown(best_positive);
                else if (has_value)
                    text = L"0m";       // 数据陈旧或刚好到点
                else
                    text = L"--";
            }
        }
        else
        {
            const WindowState& w = snap.win[m_window];
            if (!snap.ever_ok || !w.found)
            {
                text = L"--";
            }
            else if (m_kind == KIND_USED)
            {
                text = std::to_wstring(w.used) + L"%";
            }
            else
            {
                text = std::to_wstring(100 - w.used) + L"%";
            }
        }

        std::lock_guard<std::mutex> lock(m_mutex);
        m_value = text;
    }
}
