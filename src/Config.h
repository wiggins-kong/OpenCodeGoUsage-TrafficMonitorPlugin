// Config.h —— 插件设置
#pragma once

#include "TmCommon.h"

#include <mutex>
#include <string>

namespace ocg
{
    struct PluginSettings
    {
        std::wstring api_key;       // 用户在选项对话框里填的；空 = 未配置
        int interval_sec = 60;      // 刷新间隔，30..3600
        std::wstring proxy;         // 空 = 自动（环境变量 -> 系统代理 -> 直连）；"direct" = 直连；其他 = host:port
    };

    class Config
    {
    public:
        static Config& Instance();

        // dir 来自宿主传来的 EI_CONFIG_DIR（形如 <exe或%APPDATA%>\plugins\）
        void Load(const std::wstring& dir);
        bool Loaded() const { return m_loaded; }

        PluginSettings Get() const;
        void Set(const PluginSettings& s);      // 只改内存
        void Save();                            // 立即写盘

        std::wstring IniDir() const { return m_dir; }
        std::wstring IniPath() const;

        static int ClampInterval(int sec);

    private:
        Config() = default;
        std::wstring ReadIni(const wchar_t* key) const;

        std::wstring m_dir;
        PluginSettings m_settings;
        bool m_loaded = false;
        mutable std::mutex m_mutex;
    };
}
