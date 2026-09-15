// host_sim.cpp —— 模拟宿主的最小测试程序
//
// 做真实 TrafficMonitor 会做的事：LoadLibrary -> TMPluginGetInstance -> 枚举显示项目 ->
// 传 EI_CONFIG_DIR -> 在另一个线程里反复调用 DataRequired（宿主就是这么干的）——
// 同时主线程不停地读 GetItemValueText/GetTooltipInfo，用来暴露数据竞争。
//
// 用法: host_sim.exe [dll路径] [配置目录] [观察秒数] [--dialog]
#include "../src/TmSdk.h"

#include <windows.h>
#include <cstdio>
#include <cstdlib>
#include <cwctype>
#include <string>
#include <vector>

namespace
{
    typedef ITMPlugin* (*PFN_GET_INSTANCE)();

    void PrintUtf8(const char* fmt, ...)
    {
        char buf[4096];
        va_list args;
        va_start(args, fmt);
        _vsnprintf_s(buf, sizeof(buf), _TRUNCATE, fmt, args);
        va_end(args);
        fwrite(buf, 1, strlen(buf), stdout);
        fflush(stdout);
    }

    std::string ToUtf8(const wchar_t* s)
    {
        if (s == nullptr)
            return "<nullptr>";
        if (*s == L'\0')
            return "";
        const int n = ::WideCharToMultiByte(CP_UTF8, 0, s, -1, nullptr, 0, nullptr, nullptr);
        if (n <= 1)
            return "";
        std::string out((size_t)n - 1, '\0');
        ::WideCharToMultiByte(CP_UTF8, 0, s, -1, &out[0], n, nullptr, nullptr);
        return out;
    }

    struct WorkerArgs
    {
        ITMPlugin* plugin = nullptr;
        volatile LONG stop = 0;
    };

    DWORD WINAPI RequiredThread(LPVOID param)
    {
        // 宿主在监控线程上调用 DataRequired
        WorkerArgs* args = static_cast<WorkerArgs*>(param);
        while (::InterlockedCompareExchange(&args->stop, 0, 0) == 0)
        {
            args->plugin->DataRequired();
            ::Sleep(200);
        }
        return 0;
    }
}

int wmain(int argc, wchar_t** argv)
{
    const wchar_t* dllPath = (argc > 1) ? argv[1] : L"D:\\Programs\\TrafficMonitor\\plugins\\OpenCodeGoUsage.dll";
    const wchar_t* cfgDir = (argc > 2) ? argv[2] : L"obj\\testcfg\\";
    const int observeSec = (argc > 3) ? _wtoi(argv[3]) : 8;

    PrintUtf8("== 1) LoadLibrary(%s)\n", ToUtf8(dllPath).c_str());
    HMODULE mod = ::LoadLibraryW(dllPath);
    if (mod == nullptr)
    {
        PrintUtf8("   [FAIL] LoadLibrary 失败，GetLastError=%lu\n", ::GetLastError());
        return 1;
    }
    PrintUtf8("   OK\n");

    PFN_GET_INSTANCE getInstance = reinterpret_cast<PFN_GET_INSTANCE>(
        reinterpret_cast<void*>(::GetProcAddress(mod, "TMPluginGetInstance")));
    if (getInstance == nullptr)
    {
        PrintUtf8("   [FAIL] 找不到导出函数 TMPluginGetInstance\n");
        return 1;
    }
    ITMPlugin* plugin = getInstance();
    if (plugin == nullptr)
    {
        PrintUtf8("   [FAIL] TMPluginGetInstance 返回空\n");
        return 1;
    }
    PrintUtf8("   OK，插件对象=%p\n", (void*)plugin);

    PrintUtf8("\n== 2) 插件信息\n");
    PrintUtf8("   API version = %d\n", plugin->GetAPIVersion());
    PrintUtf8("   名称   : %s\n", ToUtf8(plugin->GetInfo(ITMPlugin::TMI_NAME)).c_str());
    PrintUtf8("   版本   : %s\n", ToUtf8(plugin->GetInfo(ITMPlugin::TMI_VERSION)).c_str());
    PrintUtf8("   作者   : %s\n", ToUtf8(plugin->GetInfo(ITMPlugin::TMI_AUTHOR)).c_str());
    PrintUtf8("   描述   : %s\n", ToUtf8(plugin->GetInfo(ITMPlugin::TMI_DESCRIPTION)).c_str());
    for (int i = 0; i < ITMPlugin::TMI_MAX; ++i)
    {
        if (plugin->GetInfo(static_cast<ITMPlugin::PluginInfoIndex>(i)) == nullptr)
        {
            PrintUtf8("   [FAIL] GetInfo(%d) 返回了 nullptr（宿主不允许）\n", i);
            return 1;
        }
    }
    PrintUtf8("   所有 GetInfo 字段都非空 OK\n");

    PrintUtf8("\n== 3) 传 EI_CONFIG_DIR = %s\n", ToUtf8(cfgDir).c_str());
    plugin->OnExtenedInfo(ITMPlugin::EI_CONFIG_DIR, cfgDir);

    PrintUtf8("\n== 4) 枚举显示项目（宿主会一直取到 nullptr）\n");
    int itemCount = 0;
    for (int i = 0;; ++i)
    {
        IPluginItem* item = plugin->GetItem(i);
        if (item == nullptr)
            break;
        const wchar_t* id = item->GetItemId();
        const wchar_t* name = item->GetItemName();
        const wchar_t* label = item->GetItemLableText();
        const wchar_t* sample = item->GetItemValueSampleText();
        const wchar_t* value = item->GetItemValueText();
        if (id == nullptr || name == nullptr || label == nullptr || sample == nullptr || value == nullptr)
        {
            PrintUtf8("   [FAIL] 第 %d 个条目有 getter 返回了 nullptr\n", i);
            return 1;
        }
        for (const wchar_t* p = id; *p; ++p)     // 宿主把 ID 写进 ini，约定只允许字母和数字
        {
            if (!iswalnum(*p))
            {
                PrintUtf8("   [FAIL] 条目 ID \"%s\" 含非字母数字字符\n", ToUtf8(id).c_str());
                return 1;
            }
        }
        PrintUtf8("   [%d] id=%-10s 标签=%-6s 值=%-7s 示例=%-7s 名称=%s\n",
                  i, ToUtf8(id).c_str(), ToUtf8(label).c_str(), ToUtf8(value).c_str(),
                  ToUtf8(sample).c_str(), ToUtf8(name).c_str());
        ++itemCount;
    }
    PrintUtf8("   共 %d 个条目\n", itemCount);
    if (itemCount != 9)
    {
        PrintUtf8("   [FAIL] 期望 9 个条目（3 个窗口 × 已用/剩余/重置倒计时）\n");
        return 1;
    }
    if (plugin->GetItem(itemCount) != nullptr || plugin->GetItem(-1) != nullptr)
    {
        PrintUtf8("   [FAIL] 越界的 GetItem 必须返回 nullptr\n");
        return 1;
    }
    PrintUtf8("   越界索引返回 nullptr OK\n");

    PrintUtf8("\n== 5) 插件命令\n");
    const int cmdCount = plugin->GetCommandCount();
    for (int i = 0; i < cmdCount; ++i)
        PrintUtf8("   [%d] %s\n", i, ToUtf8(plugin->GetCommandName(i)).c_str());
    PrintUtf8("   越界命令名 = %s\n", plugin->GetCommandName(cmdCount) == nullptr ? "(nullptr，符合预期)" : "(非空，异常)");

    if (argc > 4 && wcscmp(argv[4], L"--dialog") == 0)
    {
        PrintUtf8("\n== 5b) 打开插件选项对话框（人工看布局/中文渲染；关掉它才会继续）\n");
        const ITMPlugin::OptionReturn r = plugin->ShowOptionsDialog(nullptr);
        PrintUtf8("   ShowOptionsDialog 返回 %d（0=CHANGED 1=UNCHANGED 2=NOT_PROVIDED）\n", (int)r);
    }

    PrintUtf8("\n== 6) 在另一个线程里每 200ms 调一次 DataRequired，主线程同时读值（共 %d 秒）\n", observeSec);
    WorkerArgs args;
    args.plugin = plugin;
    HANDLE worker = ::CreateThread(nullptr, 0, &RequiredThread, &args, 0, nullptr);

    for (int t = 0; t < observeSec * 5; ++t)
    {
        ::Sleep(200);
        std::string line;
        for (int i = 0; i < itemCount; ++i)
        {
            IPluginItem* item = plugin->GetItem(i);
            line += ToUtf8(item->GetItemValueText());
            if (i + 1 < itemCount)
                line += " | ";
        }
        PrintUtf8("   t=%4.1fs  %s\n", (t + 1) * 0.2, line.c_str());
    }

    ::InterlockedExchange(&args.stop, 1);
    ::WaitForSingleObject(worker, 5000);
    ::CloseHandle(worker);

    PrintUtf8("\n== 6b) 占用图（宿主取 GetResourceUsageGraphValue()*100 画虚线框+填充条）\n");
    {
        int bad = 0;
        for (int i = 0; i < itemCount; ++i)
        {
            IPluginItem* item = plugin->GetItem(i);
            const std::string text = ToUtf8(item->GetItemValueText());
            const bool draws = item->IsDrawResourceUsageGraph() != 0;
            const int bar = static_cast<int>(item->GetResourceUsageGraphValue() * 100.0f);

            std::string note;
            bool ok = true;
            if (text.find('%') != std::string::npos)
            {
                // 有数据的百分比条目：条的长度必须等于文字里的数字（53、59 是 float 陷阱）
                const int shown = std::atoi(text.c_str());
                char buf[64];
                _snprintf_s(buf, sizeof(buf), _TRUNCATE, "文字 %d%% / 条 %d%%", shown, bar);
                note = buf;
                ok = draws && bar == shown;
            }
            else
            {
                // 没有数据（--）或者倒计时条目：不该画图，宿主对 0 值仍会画一个空虚线框
                note = draws ? "不该画却画了" : "不画（符合预期）";
                ok = !draws && bar == 0;
            }
            PrintUtf8("   [%d] id=%-10s %s%s\n", i, ToUtf8(item->GetItemId()).c_str(), note.c_str(),
                      ok ? "  OK" : "  [FAIL]");
            if (!ok)
                ++bad;
        }
        if (bad != 0)
        {
            PrintUtf8("   [FAIL] %d 个条目的占用图不符合预期\n", bad);
            return 1;
        }
    }

    PrintUtf8("\n== 7) tooltip（宿主会把它拼进任务栏提示）\n");
    const wchar_t* tip = plugin->GetTooltipInfo();
    if (tip == nullptr)
    {
        PrintUtf8("   [FAIL] GetTooltipInfo 返回 nullptr\n");
        return 1;
    }
    {
        std::string t = ToUtf8(tip);
        size_t start = 0;
        while (start <= t.size())
        {
            size_t nl = t.find("\r\n", start);
            if (nl == std::string::npos)
                nl = t.size();
            PrintUtf8("   | %s\n", t.substr(start, nl - start).c_str());
            if (nl == t.size())
                break;
            start = nl + 2;
        }
    }

    PrintUtf8("\n== 7b) 三个重置倒计时条目 vs tooltip 里各窗口自己的倒计时\n");
    {
        // 条目值取错窗口（比如三个都读 rolling）在这里会立刻暴露：
        // tooltip 第 1~3 行固定是 5h / 周 / 月，行尾的 "（2h13m 后）" 就是该窗口的倒计时
        struct ResetItem
        {
            const wchar_t* id;
            const char* line_prefix;    // 该窗口在 tooltip 里的行首（含全角冒号）
        };
        const ResetItem resets[3] = {
            { L"ocg5hreset", "5h：" },
            { L"ocgwreset",  "周：" },
            { L"ocgmreset",  "月：" },
        };

        std::vector<std::string> lines;
        {
            const std::string t = ToUtf8(tip);
            size_t start = 0;
            while (start <= t.size())
            {
                size_t nl = t.find("\r\n", start);
                if (nl == std::string::npos)
                    nl = t.size();
                lines.push_back(t.substr(start, nl - start));
                if (nl == t.size())
                    break;
                start = nl + 2;
            }
        }

        int bad = 0;
        for (int i = 0; i < 3; ++i)
        {
            const size_t li = (size_t)i + 1;
            const std::string line = (li < lines.size()) ? lines[li] : std::string();

            IPluginItem* item = nullptr;
            for (int k = 0; k < itemCount; ++k)
            {
                IPluginItem* cand = plugin->GetItem(k);
                if (cand != nullptr && wcscmp(cand->GetItemId(), resets[i].id) == 0)
                {
                    item = cand;
                    break;
                }
            }

            const std::string value = (item != nullptr) ? ToUtf8(item->GetItemValueText())
                                                        : std::string("<没有这个条目>");

            // 从 "（" 到 " 后）" 之间取该窗口的倒计时；没有重置时刻时这段不存在
            std::string tip_cd;
            const size_t open = line.find("（");
            const size_t close = line.find(" 后）");
            if (open != std::string::npos && close != std::string::npos && close > open + 3)
                tip_cd = line.substr(open + 3, close - open - 3);

            bool ok = item != nullptr;
            if (ok && line.rfind(resets[i].line_prefix, 0) != 0)
            {
                PrintUtf8("   [FAIL] tooltip 第 %d 行不是「%s」开头的窗口行：%s\n",
                          i + 1, resets[i].line_prefix, line.c_str());
                ++bad;
                continue;
            }
            if (ok)
                ok = (tip_cd == value) || (tip_cd.empty() && value == "--");

            PrintUtf8("   [%d] %-10s 条目=%-7s tooltip=%-7s%s\n", i, ToUtf8(resets[i].id).c_str(),
                      value.c_str(), tip_cd.empty() ? "(无)" : tip_cd.c_str(), ok ? "  OK" : "  [FAIL]");
            if (!ok)
                ++bad;
        }
        if (bad != 0)
        {
            PrintUtf8("   [FAIL] 倒计时条目与 tooltip 对不上（没数据时两边都该是 -- / 空）\n");
            return 1;
        }
    }

    PrintUtf8("\n== 8) 卸载\n");
    ::FreeLibrary(mod);     // 插件把 DLL 钉住了，这次调用实际是空操作，但不能崩
    PrintUtf8("   FreeLibrary 返回，未崩溃 OK\n");

    PrintUtf8("\n全部检查通过\n");
    return 0;
}
