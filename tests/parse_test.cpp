// parse_test.cpp —— UsageParse/JsonScan 的单元测试（控制台，无依赖）
// 编译： cl /nologo /EHsc /std:c++17 /utf-8 /W4 /I..\src parse_test.cpp ..\src\UsageParse.cpp
#include "../src/UsageParse.h"
#include "../src/JsonScan.h"

#include <cstdio>
#include <string>

static int g_failed = 0;
static int g_total = 0;

#define CHECK(cond)                                                            \
    do {                                                                       \
        ++g_total;                                                             \
        if (!(cond)) {                                                         \
            ++g_failed;                                                        \
            std::printf("  [FAIL] line %d: %s\n", __LINE__, #cond);            \
        }                                                                      \
    } while (0)

#define CHECK_EQ_STR(actual, expected)                                         \
    do {                                                                       \
        ++g_total;                                                             \
        const std::string a_ = (actual);                                       \
        const std::string e_ = (expected);                                     \
        if (a_ != e_) {                                                        \
            ++g_failed;                                                        \
            std::printf("  [FAIL] line %d: got \"%s\", want \"%s\"\n",         \
                        __LINE__, a_.c_str(), e_.c_str());                     \
        }                                                                      \
    } while (0)

#define CHECK_EQ_INT(actual, expected)                                         \
    do {                                                                       \
        ++g_total;                                                             \
        const long long a_ = (long long)(actual);                              \
        const long long e_ = (long long)(expected);                            \
        if (a_ != e_) {                                                        \
            ++g_failed;                                                        \
            std::printf("  [FAIL] line %d: got %lld, want %lld\n",             \
                        __LINE__, a_, e_);                                     \
        }                                                                      \
    } while (0)

// 接口文档/已有第三方实现里给出的形态
static const char* kOkBody =
    "{\"usage\":{\"rolling\":{\"status\":\"ok\",\"percent\":1,\"resetsAt\":\"2026-08-15T12:34:47.397Z\"},"
    "\"weekly\":{\"status\":\"ok\",\"percent\":18,\"resetsAt\":\"2026-08-17T00:00:00.397Z\"},"
    "\"monthly\":{\"status\":\"ok\",\"percent\":11,\"resetsAt\":\"2026-09-01T01:15:47.397Z\"}}}";

// 本机实测拿到的真实 401 响应体
static const char* kAuthErrBody =
    "{\"type\":\"error\",\"error\":{\"type\":\"AuthError\",\"message\":\"Unauthorized\"}}";

static const char* kNoKeyBody =
    "{\"type\":\"error\",\"error\":{\"type\":\"AuthError\",\"message\":\"Missing API key.\"}}";

static void TestOkBody()
{
    std::printf("TestOkBody\n");
    ocg::UsageResult r;
    CHECK(ocg::ParseUsage(kOkBody, 200, r));
    CHECK(!r.is_error);
    CHECK_EQ_INT(r.http_status, 200);
    for (int i = 0; i < ocg::WIN_COUNT; ++i)
        CHECK(r.windows[i].found);
    CHECK_EQ_INT(r.windows[ocg::WIN_ROLLING].percent, 1);
    CHECK_EQ_INT(r.windows[ocg::WIN_WEEKLY].percent, 18);
    CHECK_EQ_INT(r.windows[ocg::WIN_MONTHLY].percent, 11);
    CHECK_EQ_STR(r.windows[ocg::WIN_ROLLING].status, "ok");
    CHECK_EQ_STR(r.windows[ocg::WIN_ROLLING].resets_at, "2026-08-15T12:34:47.397Z");
    CHECK_EQ_STR(r.windows[ocg::WIN_MONTHLY].resets_at, "2026-09-01T01:15:47.397Z");
}

static void TestErrorBodies()
{
    std::printf("TestErrorBodies\n");
    ocg::UsageResult r;
    CHECK(ocg::ParseUsage(kAuthErrBody, 401, r));
    CHECK(r.is_error);
    CHECK_EQ_INT(r.http_status, 401);
    CHECK_EQ_STR(r.error_type, "AuthError");
    CHECK_EQ_STR(r.error_message, "Unauthorized");

    ocg::UsageResult r2;
    CHECK(ocg::ParseUsage(kNoKeyBody, 401, r2));
    CHECK(r2.is_error);
    CHECK_EQ_STR(r2.error_message, "Missing API key.");
}

static void TestPrettyPrinted()
{
    std::printf("TestPrettyPrinted\n");
    // 服务端换行/缩进，且成员顺序被打乱、数值周围有空格
    const std::string body =
        "{\n"
        "  \"usage\": {\n"
        "    \"weekly\": { \"resetsAt\" : \"2026-08-17T00:00:00.000Z\" , \"percent\" :   18 , \"status\" : \"rate-limited\" },\n"
        "    \"rolling\": {\n"
        "      \"percent\": 100,\n"
        "      \"status\": \"ok\",\n"
        "      \"resetsAt\": \"2026-08-15T12:34:47.397Z\"\n"
        "    },\n"
        "    \"monthly\": { \"status\": \"ok\", \"percent\": 0, \"resetsAt\": \"\" }\n"
        "  }\n"
        "}";
    ocg::UsageResult r;
    CHECK(ocg::ParseUsage(body, 200, r));
    CHECK(!r.is_error);
    CHECK(r.windows[ocg::WIN_WEEKLY].found);
    CHECK_EQ_INT(r.windows[ocg::WIN_WEEKLY].percent, 18);
    CHECK_EQ_STR(r.windows[ocg::WIN_WEEKLY].status, "rate-limited");
    CHECK_EQ_INT(r.windows[ocg::WIN_ROLLING].percent, 100);
    CHECK_EQ_INT(r.windows[ocg::WIN_MONTHLY].percent, 0);
    CHECK_EQ_STR(r.windows[ocg::WIN_MONTHLY].resets_at, "");
}

static void TestNestedSameNameKeys()
{
    std::printf("TestNestedSameNameKeys\n");
    // usage 里若有嵌套对象的 percent/status，不能污染窗口解析；
    // 且顶层若另有同名字段也不该被误取
    const std::string body =
        "{\"usage\":{\"percent\":999,\"status\":\"WRONG\","
        "\"rolling\":{\"status\":\"ok\",\"percent\":7,\"resetsAt\":\"2026-08-15T12:34:47.397Z\"},"
        "\"meta\":{\"percent\":42,\"status\":\"WRONG2\"}},"
        "\"percent\":888}";
    ocg::UsageResult r;
    CHECK(ocg::ParseUsage(body, 200, r));
    CHECK_EQ_INT(r.windows[ocg::WIN_ROLLING].percent, 7);
    CHECK_EQ_STR(r.windows[ocg::WIN_ROLLING].status, "ok");
    CHECK(!r.windows[ocg::WIN_WEEKLY].found);
}

static void TestEscapesInMessage()
{
    std::printf("TestEscapesInMessage\n");
    // message 里带引号、右花括号、中文转义 —— 花括号不能骗过括号配对
    const std::string body =
        "{\"type\":\"error\",\"error\":{\"type\":\"EntitlementError\","
        "\"message\":\"OpenCode Go subscription required. } \\\"quoted\\\" \\u4e2d\\u6587\"}}";
    ocg::UsageResult r;
    CHECK(ocg::ParseUsage(body, 403, r));
    CHECK(r.is_error);
    CHECK_EQ_STR(r.error_type, "EntitlementError");
    CHECK_EQ_STR(r.error_message, "OpenCode Go subscription required. } \"quoted\" \xe4\xb8\xad\xe6\x96\x87");

    // 带 UTF-8 原文（非转义）的消息
    const std::string body2 =
        "{\"type\":\"error\",\"error\":{\"type\":\"AuthError\",\"message\":\"Key 无效\"}}";
    ocg::UsageResult r2;
    CHECK(ocg::ParseUsage(body2, 401, r2));
    CHECK_EQ_STR(r2.error_message, "Key \xe6\x97\xa0\xe6\x95\x88");
}

static void TestGarbageAndPartial()
{
    std::printf("TestGarbageAndPartial\n");
    // HTML 错误页
    ocg::UsageResult r;
    CHECK(!ocg::ParseUsage("<html><body>502 Bad Gateway</body></html>", 502, r));
    CHECK(r.is_error);
    CHECK_EQ_INT(r.http_status, 502);

    // 空体
    ocg::UsageResult r2;
    CHECK(!ocg::ParseUsage("", 500, r2));
    CHECK(r2.is_error);

    // 200 但没有 usage
    ocg::UsageResult r3;
    CHECK(!ocg::ParseUsage("{\"foo\":1}", 200, r3));
    CHECK(r3.is_error);

    // 只有部分窗口
    ocg::UsageResult r4;
    CHECK(ocg::ParseUsage("{\"usage\":{\"rolling\":{\"percent\":3,\"status\":\"ok\"}}}", 200, r4));
    CHECK(r4.windows[ocg::WIN_ROLLING].found);
    CHECK(!r4.windows[ocg::WIN_WEEKLY].found);
    CHECK_EQ_STR(r4.windows[ocg::WIN_ROLLING].resets_at, "");

    // percent 是负数 / 超过 100（不该崩，也不该乱）
    ocg::UsageResult r5;
    CHECK(ocg::ParseUsage("{\"usage\":{\"rolling\":{\"percent\":-5,\"status\":\"ok\",\"resetsAt\":\"x\"}}}", 200, r5));
    CHECK_EQ_INT(r5.windows[ocg::WIN_ROLLING].percent, -5);
}

static void TestJsonScanBasics()
{
    std::printf("TestJsonScanBasics\n");
    using namespace jsonscan;
    const std::string s = "{\"a\":1,\"b\":{\"c\":[1,2,{\"d\":\"x}y\"}]},\"e\":\"v\",\"f\":null}";
    const Pos b = FindMember(s, 0, s.size(), "b");
    CHECK(b != kNotFound);
    CHECK_EQ_INT(CompositeEnd(s, b), s.find("},\"e\"") + 1);
    const Pos e = FindMember(s, 0, s.size(), "e");
    CHECK(e != kNotFound);
    std::string v;
    CHECK(GetString(s, e, v));
    CHECK_EQ_STR(v, "v");

    // 数组里的花括号/引号不能破坏配对
    Pos i = 0;
    CHECK(SkipComposite(s, i));
    CHECK_EQ_INT(i, s.size());
}

int main()
{
    TestOkBody();
    TestErrorBodies();
    TestPrettyPrinted();
    TestNestedSameNameKeys();
    TestEscapesInMessage();
    TestGarbageAndPartial();
    TestJsonScanBasics();

    std::printf("\n%d/%d 通过%s\n", g_total - g_failed, g_total, g_failed ? "  <<< 有失败" : "");
    return g_failed ? 1 : 0;
}
