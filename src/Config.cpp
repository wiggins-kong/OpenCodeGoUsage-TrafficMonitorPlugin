#include "Config.h"
#include "PluginGlobals.h"

#include <vector>

namespace ocg
{
    namespace
    {
        const wchar_t* const kIniSection = L"config";
        const wchar_t* const kIniKeyApiKey = L"api_key";
        const wchar_t* const kIniKeyInterval = L"interval_sec";
        const wchar_t* const kIniKeyProxy = L"proxy";

        std::wstring ReadIniValue(const std::wstring& path, const wchar_t* key)
        {
            std::vector<wchar_t> buf(4096, L'\0');
            const DWORD n = ::GetPrivateProfileStringW(kIniSection, key, L"", buf.data(), (DWORD)buf.size(), path.c_str());
            return std::wstring(buf.data(), n);
        }

        void WriteIniValue(const std::wstring& path, const wchar_t* key, const std::wstring& value)
        {
            ::WritePrivateProfileStringW(kIniSection, key, value.c_str(), path.c_str());
        }
    }

    Config& Config::Instance()
    {
        static Config instance;
        return instance;
    }

    int Config::ClampInterval(int sec)
    {
        if (sec < 30)
            return 30;
        if (sec > 3600)
            return 3600;
        return sec;
    }

    std::wstring Config::IniPath() const
    {
        // 与 PluginDemo 的 DataManager 一致：配置文件名取自 DLL 文件名
        std::wstring dir = m_dir;
        if (!dir.empty() && dir.back() != L'\\' && dir.back() != L'/')
            dir.push_back(L'\\');
        return dir + tmu::FileNameNoExt(tmu::GetModulePath(g_hModule)) + L".ini";
    }

    void Config::Load(const std::wstring& dir)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_dir = dir;

        const std::wstring path = IniPath();
        PluginSettings s;
        s.api_key = tmu::Trim(ReadIniValue(path, kIniKeyApiKey));
        s.interval_sec = ClampInterval(::GetPrivateProfileIntW(kIniSection, kIniKeyInterval, 60, path.c_str()));
        s.proxy = tmu::Trim(ReadIniValue(path, kIniKeyProxy));
        m_settings = s;
        m_loaded = true;
    }

    PluginSettings Config::Get() const
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_settings;
    }

    void Config::Set(const PluginSettings& s)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_settings = s;
        m_settings.interval_sec = ClampInterval(m_settings.interval_sec);
        m_settings.api_key = tmu::Trim(m_settings.api_key);
        m_settings.proxy = tmu::Trim(m_settings.proxy);
    }

    void Config::Save()
    {
        PluginSettings s;
        std::wstring path;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            s = m_settings;
            path = IniPath();
        }
        WriteIniValue(path, kIniKeyApiKey, s.api_key);
        WriteIniValue(path, kIniKeyInterval, std::to_wstring(s.interval_sec));
        WriteIniValue(path, kIniKeyProxy, s.proxy);
    }
}
