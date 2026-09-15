// JsonScan.h —— 极简 JSON 取值工具
//
// 整个插件只需要从响应里读 6 个字段，为此引入 nlohmann/json 之类的库会让 DLL 多出一堆
// 编译单元和依赖，不划算。这里只实现"在本层查找某个成员的值"这一件事：
// 扫描时会整段跳过嵌套的复合值与字符串内容，所以不会误命中子对象里的同名键。
#pragma once

#include <string>
#include <cstdlib>

namespace jsonscan
{
    typedef size_t Pos;
    const Pos kNotFound = std::string::npos;

    inline void SkipWs(const std::string& s, Pos& i)
    {
        while (i < s.size())
        {
            const char c = s[i];
            if (c == ' ' || c == '\t' || c == '\r' || c == '\n')
                ++i;
            else
                break;
        }
    }

    // i 指向起始引号；成功时 i 停在闭合引号之后
    inline bool SkipString(const std::string& s, Pos& i)
    {
        if (i >= s.size() || s[i] != '"')
            return false;
        ++i;
        while (i < s.size())
        {
            const char c = s[i];
            if (c == '\\')
            {
                i += 2;         // 跳过被转义的字符（\"  \\  \uXXXX 等）
                continue;
            }
            if (c == '"')
            {
                ++i;
                return true;
            }
            ++i;
        }
        return false;
    }

    // i 指向 '{' 或 '['；成功时 i 停在配对的 '}' / ']' 之后
    inline bool SkipComposite(const std::string& s, Pos& i)
    {
        if (i >= s.size() || (s[i] != '{' && s[i] != '['))
            return false;
        int depth = 0;
        while (i < s.size())
        {
            const char c = s[i];
            if (c == '"')
            {
                if (!SkipString(s, i))
                    return false;
                continue;
            }
            if (c == '{' || c == '[')
            {
                ++depth;
            }
            else if (c == '}' || c == ']')
            {
                --depth;
                if (depth == 0)
                {
                    ++i;
                    return true;
                }
            }
            ++i;
        }
        return false;
    }

    // 跳过任意一个值（标量 / 字符串 / 复合值）
    inline bool SkipValue(const std::string& s, Pos& i)
    {
        SkipWs(s, i);
        if (i >= s.size())
            return false;
        const char c = s[i];
        if (c == '"')
            return SkipString(s, i);
        if (c == '{' || c == '[')
            return SkipComposite(s, i);
        while (i < s.size())
        {
            const char d = s[i];
            if (d == ',' || d == '}' || d == ']' || d == ' ' || d == '\t' || d == '\r' || d == '\n')
                break;
            ++i;
        }
        return true;
    }

    // 在 s[begin, end) —— 一个对象的内容（begin 通常指向 '{'）—— 的本层查找 key，
    // 命中则返回值的起始下标，否则返回 kNotFound
    inline Pos FindMember(const std::string& s, Pos begin, Pos end, const std::string& key)
    {
        Pos i = begin;
        SkipWs(s, i);
        if (i >= end || s[i] != '{')
            return kNotFound;
        ++i;
        while (i < end)
        {
            SkipWs(s, i);
            if (i >= end)
                return kNotFound;
            if (s[i] == '}')
                return kNotFound;
            if (s[i] == ',')
            {
                ++i;
                continue;
            }
            if (s[i] != '"')
                return kNotFound;               // 格式异常，放弃

            const Pos keyStart = i;
            if (!SkipString(s, i))
                return kNotFound;
            const std::string curKey = s.substr(keyStart + 1, i - keyStart - 2);

            SkipWs(s, i);
            if (i >= end || s[i] != ':')
                return kNotFound;
            ++i;
            SkipWs(s, i);
            const Pos valuePos = i;

            if (curKey == key)
                return valuePos;

            if (!SkipValue(s, i))               // 跳过这个成员的值（含嵌套对象整体）
                return kNotFound;
        }
        return kNotFound;
    }

    // 取一个 '{' / '[' 的结束位置（返回配对的 '}' / ']' 之后的下标）
    inline Pos CompositeEnd(const std::string& s, Pos begin)
    {
        Pos i = begin;
        if (!SkipComposite(s, i))
            return s.size();
        return i;
    }

    // 解析 JSON 字符串字面量（含转义，\uXXXX 会转成 UTF-8）
    inline bool GetString(const std::string& s, Pos valuePos, std::string& out)
    {
        out.clear();
        Pos i = valuePos;
        if (i >= s.size() || s[i] != '"')
            return false;
        ++i;
        while (i < s.size())
        {
            const char c = s[i];
            if (c == '"')
                return true;
            if (c != '\\')
            {
                out.push_back(c);
                ++i;
                continue;
            }

            if (i + 1 >= s.size())
                return false;
            const char esc = s[i + 1];
            switch (esc)
            {
            case '"':  out.push_back('"');  i += 2; break;
            case '\\': out.push_back('\\'); i += 2; break;
            case '/':  out.push_back('/');  i += 2; break;
            case 'b':  out.push_back('\b'); i += 2; break;
            case 'f':  out.push_back('\f'); i += 2; break;
            case 'n':  out.push_back('\n'); i += 2; break;
            case 'r':  out.push_back('\r'); i += 2; break;
            case 't':  out.push_back('\t'); i += 2; break;
            case 'u':
            {
                if (i + 5 >= s.size())
                    return false;
                unsigned int cp = 0;
                for (int k = 0; k < 4; ++k)
                {
                    const char h = s[i + 2 + k];
                    cp <<= 4;
                    if (h >= '0' && h <= '9')      cp |= (unsigned int)(h - '0');
                    else if (h >= 'a' && h <= 'f') cp |= (unsigned int)(h - 'a' + 10);
                    else if (h >= 'A' && h <= 'F') cp |= (unsigned int)(h - 'A' + 10);
                    else return false;
                }
                i += 6;

                // 代理对：把 \uD800-\uDBFF + \uDC00-\uDFFF 合成一个码点
                if (cp >= 0xD800 && cp <= 0xDBFF && i + 1 < s.size() && s[i] == '\\' && s[i + 1] == 'u' && i + 5 < s.size())
                {
                    unsigned int lo = 0;
                    bool ok = true;
                    for (int k = 0; k < 4; ++k)
                    {
                        const char h = s[i + 2 + k];
                        lo <<= 4;
                        if (h >= '0' && h <= '9')      lo |= (unsigned int)(h - '0');
                        else if (h >= 'a' && h <= 'f') lo |= (unsigned int)(h - 'a' + 10);
                        else if (h >= 'A' && h <= 'F') lo |= (unsigned int)(h - 'A' + 10);
                        else { ok = false; break; }
                    }
                    if (ok && lo >= 0xDC00 && lo <= 0xDFFF)
                    {
                        cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                        i += 6;
                    }
                }

                // 码点 -> UTF-8
                if (cp < 0x80)
                {
                    out.push_back((char)cp);
                }
                else if (cp < 0x800)
                {
                    out.push_back((char)(0xC0 | (cp >> 6)));
                    out.push_back((char)(0x80 | (cp & 0x3F)));
                }
                else if (cp < 0x10000)
                {
                    out.push_back((char)(0xE0 | (cp >> 12)));
                    out.push_back((char)(0x80 | ((cp >> 6) & 0x3F)));
                    out.push_back((char)(0x80 | (cp & 0x3F)));
                }
                else
                {
                    out.push_back((char)(0xF0 | (cp >> 18)));
                    out.push_back((char)(0x80 | ((cp >> 12) & 0x3F)));
                    out.push_back((char)(0x80 | ((cp >> 6) & 0x3F)));
                    out.push_back((char)(0x80 | (cp & 0x3F)));
                }
                break;
            }
            default:
                return false;
            }
        }
        return false;
    }

    // 解析整数（只支持整型；percent 就是整数）
    inline bool GetInt(const std::string& s, Pos valuePos, int& out)
    {
        Pos i = valuePos;
        if (i >= s.size())
            return false;
        const char* start = s.c_str() + i;
        char* endPtr = nullptr;
        const long v = std::strtol(start, &endPtr, 10);
        if (endPtr == start)
            return false;
        out = (int)v;
        return true;
    }
}
