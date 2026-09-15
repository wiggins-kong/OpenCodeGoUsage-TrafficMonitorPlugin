// GraphValue.h —— 百分比 → 占用图的值（纯 C++，不依赖 Windows）
//
// 宿主是这么取值的（TrafficMonitor/TaskBarDlg.cpp:326）：
//     int figure_value = item->GetResourceUsageGraphValue() * 100;
// float 乘完直接截断成 int，而 53/100.0f * 100 在 float 下是 52.999996 ——
// 朴素的 n/100.0f 会让 53%、59% 的条比文字短 1%。加一个比 float 误差大、
// 又比半个百分点小得多的 eps，0~100 每个值在"截断式"和"四舍五入式"宿主下都落在正确整数上
// （tests/parse_test.cpp 对两种宿主行为都做了穷举断言）。
#pragma once

namespace ocg
{
    inline float GraphValueFromPercent(int percent)
    {
        if (percent < 0)
            percent = 0;
        else if (percent > 100)
            percent = 100;

        return static_cast<float>(percent) / 100.0f + 0.0005f;
    }
}
