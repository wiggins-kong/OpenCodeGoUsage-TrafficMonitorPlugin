#include "Config.h"
#include "PluginGlobals.h"
#include "JsonScan.h"

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

    std::wstring KeyFromEnv(std::wstring& source)
    {
        wchar_t buf[512]{};
        const DWORD n = ::GetEnvironmentVariableW(L"OPENCODE_API_KEY", buf, _countof(buf));
        if (n == 0 || n >= _countof(buf))
        {
            source = L"OPENCODE_API_KEY 未设置";
            return std::wstring();
        }
        source = L"环境变量 OPENCODE_API_KEY";
        return tmu::Trim(buf);
    }

    std::wstring KeyFromAuthJson(std::wstring& source)
    {
        const std::wstring path = tmu::ExpandEnv(L"%USERPROFILE%\\.local\\share\\opencode\\auth.json");
        std::string json;
        if (!tmu::ReadWholeFile(path, json) || json.empty())
        {
            source = L"没有 auth.json";
            return std::wstring();
        }

        using namespace jsonscan;
        const Pos goPos = FindMember(json, 0, json.size(), "opencode-go");
        if (goPos == kNotFound)
        {
            source = L"auth.json 里没有 opencode-go";
            return std::wstring();
        }
        const Pos goEnd = CompositeEnd(json, goPos);
        const Pos keyPos = FindMember(json, goPos, goEnd, "key");
        std::string key;
        if (keyPos == kNotFound || !GetString(json, keyPos, key) || key.empty())
        {
            source = L"auth.json 的 opencode-go 没有 key";
            return std::wstring();
        }
        source = L"auth.json 的 opencode-go.key";
        return tmu::Trim(tmu::Utf8ToWide(key));
    }

    KeyInfo Config::ResolveKey() const
    {
        KeyInfo info;

        // 1) 用户在选项对话框里填的
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            if (!m_settings.api_key.empty())
            {
                info.key = m_settings.api_key;
                info.source = L"插件选项里填写的 Key";
                return info;
            }
        }

        // 2) 环境变量
        std::wstring src;
        std::wstring key = KeyFromEnv(src);
        if (!key.empty())
        {
            info.key = key;
            info.source = L"环境变量 OPENCODE_API_KEY";
            return info;
        }

        // 3) auth.json
        key = KeyFromAuthJson(src);
        if (!key.empty())
        {
            info.key = key;
            info.source = L"auth.json（opencode-go）";
            return info;
        }

        info.source = L"未配置（" + src + L"）";
        return info;
    }
}
