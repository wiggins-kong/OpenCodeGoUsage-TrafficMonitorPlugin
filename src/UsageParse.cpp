#include "UsageParse.h"
#include "JsonScan.h"

namespace ocg
{
    namespace
    {
        using namespace jsonscan;

        const char* const kWindowKeys[WIN_COUNT] = { "rolling", "weekly", "monthly" };

        // 解析一个窗口对象（形如 {"status":"ok","percent":1,"resetsAt":"..."}）
        void ParseWindow(const std::string& body, Pos objPos, UsageWindow& w)
        {
            const Pos objEnd = CompositeEnd(body, objPos);

            Pos p = FindMember(body, objPos, objEnd, "status");
            if (p != kNotFound)
                GetString(body, p, w.status);

            p = FindMember(body, objPos, objEnd, "percent");
            if (p != kNotFound)
                w.found = GetInt(body, p, w.percent);

            p = FindMember(body, objPos, objEnd, "resetsAt");
            if (p != kNotFound)
                GetString(body, p, w.resets_at);
        }
    }

    const char* WindowKeyName(int index)
    {
        if (index < 0 || index >= WIN_COUNT)
            return "";
        return kWindowKeys[index];
    }

    bool ParseUsage(const std::string& body, int http_status, UsageResult& out)
    {
        out = UsageResult();
        out.http_status = http_status;

        // 1) 错误体：{"type":"error","error":{"type":"...","message":"..."}}
        const Pos errPos = FindMember(body, 0, body.size(), "error");
        if (errPos != kNotFound)
        {
            out.is_error = true;
            const Pos errEnd = CompositeEnd(body, errPos);

            Pos p = FindMember(body, errPos, errEnd, "type");
            if (p != kNotFound)
                GetString(body, p, out.error_type);

            p = FindMember(body, errPos, errEnd, "message");
            if (p != kNotFound)
                GetString(body, p, out.error_message);

            // 有些错误体把 error 写成字符串而不是对象
            if (out.error_message.empty() && out.error_type.empty())
            {
                std::string asString;
                if (GetString(body, errPos, asString))
                    out.error_message = asString;
            }
            if (out.error_message.empty())
                out.error_message = "服务端返回了错误响应，但没有 message 字段";
            return true;
        }

        // 2) 正常体：取 usage 对象，再逐窗口取
        const Pos usagePos = FindMember(body, 0, body.size(), "usage");
        if (usagePos == kNotFound)
        {
            if (http_status != 200)
            {
                out.is_error = true;
                out.error_message = "HTTP " + std::to_string(http_status) + "，且响应体里没有可识别的错误信息";
            }
            else
            {
                out.is_error = true;
                out.error_message = "响应体里没有 usage 字段，接口格式可能已变更";
            }
            return false;
        }

        const Pos usageEnd = CompositeEnd(body, usagePos);
        for (int i = 0; i < WIN_COUNT; ++i)
        {
            const Pos wPos = FindMember(body, usagePos, usageEnd, kWindowKeys[i]);
            if (wPos != kNotFound)
                ParseWindow(body, wPos, out.windows[i]);
        }

        // HTTP 非 200 但带着 usage 体（理论上不会发生）：仍按错误处理，避免显示半截数据
        if (http_status != 200)
        {
            out.is_error = true;
            if (out.error_message.empty())
                out.error_message = "HTTP " + std::to_string(http_status);
            return true;
        }

        return true;
    }
}
