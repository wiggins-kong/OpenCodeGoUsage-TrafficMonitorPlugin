// TmCommon.h —— 公共小工具（字符串/时间/模块）
#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>

#include <string>
#include <vector>
#include <cstdio>
#include <cstdlib>

namespace tmu
{
    inline std::wstring Utf8ToWide(const std::string& s)
    {
        if (s.empty())
            return std::wstring();
        const int n = ::MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
        if (n <= 0)
            return std::wstring();
        std::wstring out((size_t)n, L'\0');
        ::MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), &out[0], n);
        return out;
    }

    inline std::string WideToUtf8(const std::wstring& s)
    {
        if (s.empty())
            return std::string();
        const int n = ::WideCharToMultiByte(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0, nullptr, nullptr);
        if (n <= 0)
            return std::string();
        std::string out((size_t)n, '\0');
        ::WideCharToMultiByte(CP_UTF8, 0, s.c_str(), (int)s.size(), &out[0], n, nullptr, nullptr);
        return out;
    }

    inline std::wstring Trim(const std::wstring& s)
    {
        size_t b = 0;
        size_t e = s.size();
        while (b < e && (s[b] == L' ' || s[b] == L'\t' || s[b] == L'\r' || s[b] == L'\n'))
            ++b;
        while (e > b && (s[e - 1] == L' ' || s[e - 1] == L'\t' || s[e - 1] == L'\r' || s[e - 1] == L'\n'))
            --e;
        return s.substr(b, e - b);
    }

    inline std::wstring ExpandEnv(const std::wstring& s)
    {
        wchar_t buf[1024]{};
        const DWORD n = ::ExpandEnvironmentStringsW(s.c_str(), buf, _countof(buf));
        if (n == 0 || n > _countof(buf))
            return s;
        return buf;
    }

    inline std::wstring GetModulePath(HMODULE mod)
    {
        wchar_t buf[1024]{};
        const DWORD n = ::GetModuleFileNameW(mod, buf, _countof(buf));
        return std::wstring(buf, n);
    }

    inline std::wstring FileNameOf(const std::wstring& path)
    {
        const size_t p = path.find_last_of(L"\\/");
        return (p == std::wstring::npos) ? path : path.substr(p + 1);
    }

    inline std::wstring FileNameNoExt(const std::wstring& path)
    {
        std::wstring name = FileNameOf(path);
        const size_t p = name.find_last_of(L'.');
        return (p == std::wstring::npos) ? name : name.substr(0, p);
    }

    // 把本 DLL 钉在内存里。宿主退出时会 FreeLibrary 插件，而我们的后台线程可能还在跑，
    // 不钉住的话线程会踩到已卸载的代码页上。
    inline void PinCurrentModule()
    {
        HMODULE mod = nullptr;
        ::GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
                             reinterpret_cast<LPCWSTR>(&PinCurrentModule), &mod);
    }

    // "2026-08-15T12:34:47.397Z" -> 本地时间
    inline bool Iso8601ToLocal(const std::string& iso, SYSTEMTIME& localOut)
    {
        int y = 0, mo = 0, d = 0, h = 0, mi = 0, sec = 0;
        if (::sscanf_s(iso.c_str(), "%4d-%2d-%2dT%2d:%2d:%2d", &y, &mo, &d, &h, &mi, &sec) != 6)
            return false;
        if (y < 1970 || mo < 1 || mo > 12 || d < 1 || d > 31)
            return false;

        SYSTEMTIME utc{};
        utc.wYear = (WORD)y;
        utc.wMonth = (WORD)mo;
        utc.wDay = (WORD)d;
        utc.wHour = (WORD)h;
        utc.wMinute = (WORD)mi;
        utc.wSecond = (WORD)sec;

        SYSTEMTIME local{};
        if (!::SystemTimeToTzSpecificLocalTime(nullptr, &utc, &local))
            return false;
        localOut = local;
        return true;
    }

    inline long long SystemTimeToTicks(const SYSTEMTIME& st)
    {
        FILETIME ft{};
        if (!::SystemTimeToFileTime(&st, &ft))
            return 0;
        ULARGE_INTEGER u{};
        u.LowPart = ft.dwLowDateTime;
        u.HighPart = ft.dwHighDateTime;
        return (long long)(u.QuadPart / 10000000ULL);   // 100ns -> 秒
    }

    // b - a（秒）
    inline long long SecondsBetween(const SYSTEMTIME& a, const SYSTEMTIME& b)
    {
        return SystemTimeToTicks(b) - SystemTimeToTicks(a);
    }

    inline std::wstring FormatCountdown(long long seconds)
    {
        if (seconds <= 0)
            return L"0m";
        if (seconds < 60)
            return L"<1m";
        const long long mins = seconds / 60;
        if (mins < 60)
            return std::to_wstring(mins) + L"m";
        const long long hours = mins / 60;
        if (hours < 24)
            return std::to_wstring(hours) + L"h" + std::to_wstring(mins % 60) + L"m";
        return std::to_wstring(hours / 24) + L"d" + std::to_wstring(hours % 24) + L"h";
    }

    inline std::wstring FormatClock(const SYSTEMTIME& st)
    {
        wchar_t buf[16]{};
        ::swprintf_s(buf, L"%02u:%02u", st.wHour, st.wMinute);
        return buf;
    }

    inline std::wstring FormatClockSeconds(const SYSTEMTIME& st)
    {
        wchar_t buf[24]{};
        ::swprintf_s(buf, L"%02u:%02u:%02u", st.wHour, st.wMinute, st.wSecond);
        return buf;
    }

    inline std::wstring FormatDateClock(const SYSTEMTIME& st)
    {
        wchar_t buf[32]{};
        ::swprintf_s(buf, L"%02u-%02u %02u:%02u", st.wMonth, st.wDay, st.wHour, st.wMinute);
        return buf;
    }

    inline const wchar_t* WeekdayName(WORD wDayOfWeek)
    {
        static const wchar_t* const kNames[7] = { L"周日", L"周一", L"周二", L"周三", L"周四", L"周五", L"周六" };
        return kNames[wDayOfWeek % 7];
    }

    // 读一个 at most count 字节的文本文件（UTF-8 原样读入）
    inline bool ReadWholeFile(const std::wstring& path, std::string& out)
    {
        out.clear();
        HANDLE h = ::CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                 nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (h == INVALID_HANDLE_VALUE)
            return false;
        char buf[4096];
        DWORD read = 0;
        while (::ReadFile(h, buf, sizeof(buf), &read, nullptr) && read > 0)
            out.append(buf, read);
        ::CloseHandle(h);
        return true;
    }
}
