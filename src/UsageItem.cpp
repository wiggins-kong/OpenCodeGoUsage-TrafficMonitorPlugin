#include "UsageItem.h"
#include "GraphValue.h"

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

    int CUsageItem::IsDrawResourceUsageGraph() const
    {
        // 没有数据时返回 0：宿主拿到 0 值仍然会把虚线框画出来，只剩一个空框配 "--" 像坏了
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_has_graph ? 1 : 0;
    }

    float CUsageItem::GetResourceUsageGraphValue() const
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_graph;
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
        float graph = 0.0f;
        bool has_graph = false;

        // 每个条目只关心自己那个窗口（倒计时也分 5h / 周 / 月三个，不再跨窗口取"最近的一次"）
        const WindowState& w = snap.win[m_window];

        if (m_kind == KIND_COUNTDOWN)
        {
            if (!snap.ever_ok || !w.found || !w.have_reset)
            {
                text = L"--";
            }
            else
            {
                SYSTEMTIME now{};
                ::GetLocalTime(&now);
                // 数据陈旧时重置时刻可能已经过去，FormatCountdown 对 <=0 秒返回 "0m"
                text = tmu::FormatCountdown(tmu::SecondsBetween(now, w.reset_local));
            }
        }
        else
        {
            if (!snap.ever_ok || !w.found)
            {
                text = L"--";
            }
            else if (m_kind == KIND_USED)
            {
                text = std::to_wstring(w.used) + L"%";
                graph = GraphValueFromPercent(w.used);
                has_graph = true;
            }
            else
            {
                text = std::to_wstring(100 - w.used) + L"%";
                graph = GraphValueFromPercent(100 - w.used);
                has_graph = true;
            }
        }

        std::lock_guard<std::mutex> lock(m_mutex);
        m_value = text;
        m_graph = graph;
        m_has_graph = has_graph;
    }
}
