# 开发交接文档（development.md）

给下一个接手的人 / agent 看的：现在做到哪了、代码怎么组织的、怎么编译验证、
哪些坑已经踩过、下一步能做什么。

- **工程位置**：`D:\Projects\OpenCodeGoUsage\`（独立工程，**不在** TrafficMonitor 仓库里，fork 保持干净）
- **当前版本**：v1.0（2026-09-15），见 [changelog.md](changelog.md)
- **部署目标**：`D:\Programs\TrafficMonitor\plugins\OpenCodeGoUsage.dll`
- **状态**：功能已完成并部署；除"带有效 key 的 200 响应在真实任务栏里的显示"外全部验证通过（见"验证清单"）

---

## 1. 一句话架构

纯 Win32 C++ 的 TrafficMonitor 插件 DLL（插件接口版本 8），后台线程每 60 秒 GET 一次
`https://opencode.ai/zen/go/v1/usage`，把三个窗口的百分比缓存成快照；
宿主的 `DataRequired()`（监控线程，每秒调）只读快照并格式化成 7 个显示条目的文本。

```
宿主监控线程 ──每秒──> CPlugin::DataRequired() ──读──> UsageService 快照（mutex 保护）
                              │                              ▲
                              └──写──> 各 CUsageItem 的文本缓存 │ 每 60s 或收到刷新信号
                                                              │
              后台线程 ──WinHTTP GET──> opencode.ai ──解析──> 快照
```

## 2. 环境事实与约束（都踩过，别再试错）

| 事实 | 影响 |
|---|---|
| 本机 TrafficMonitor 是**便携版**，在 `D:\Programs\TrafficMonitor\`，x64 | 插件必须编 x64；插件配置目录 = `plugins\`（便携模式） |
| TM 当前**以管理员身份运行**（PowerShell 读不到它的进程路径） | 插件只在启动时加载 → 改完必须重启 TM；**重启留给用户**，非管理员启动会丢掉提权（TM 靠它加载 `TrafficMonitor.sys` 读温度） |
| VS2022 Community 17.14 + v143 + Win10 SDK 26100 已装（`C:\Program Files\Microsoft Visual Studio\2022\Community\`） | 命令行用 `MSBuild\Current\Bin\MSBuild.exe`，不需要 vcvars（除了单独编 cl.exe 的小工具） |
| **VS 没装 MFC 组件**（`atlmfc` 目录不存在） | 仓库自带的 `PluginDemo` 在这台机器上**编译不过**；本插件因此是纯 Win32、零 MFC 依赖。别为了省事改成 MFC |
| `.bat` 文件必须是 **CRLF + 纯 ASCII** | Write 工具写出来的是 LF，cmd 会把 `for /f` 拆坏 → 用 heredoc 写再 `sed -i 's/$/\r/'` |
| 机器上有个失效的 Go key（明文躺在 `%APPDATA%\OpenChamber\Cache\Cache_Data\f_00001f`） | **已实测被服务端拒绝**（401 Unauthorized）。别再拿它测，去 console 重新生成 |
| `%USERPROFILE%\.local\share\opencode\auth.json` 不存在 | 插件里那条"自动嗅探 auth.json"路径目前是空转，装了 opencode CLI 才会生效 |

## 3. 文件地图

| 文件 | 职责 | 注意 |
|---|---|---|
| `src/Plugin.cpp` | `ITMPlugin` 实现、`TMPluginGetInstance` 导出、`DllMain`、7 个条目的构造、命令、tooltip 组装 | 插件对象是函数内静态对象，宿主永不析构它 |
| `src/UsageItem.cpp` | `IPluginItem` 实现，一个类靠构造参数区分 7 个条目 | 值文本在 `UpdateFrom`（监控线程）里写、`GetItemValueText`（UI 线程）里读 → 有 mutex，且返回用独立缓冲 |
| `src/UsageService.cpp` | WinHTTP 请求、后台线程循环、快照、代理探测、错误文案 | **接口 URL 在文件顶部的 `kPath` 常量**，接口变了改这里 |
| `src/UsageParse.cpp` | 响应体 → 结构体（纯 C++，不依赖 Windows） | 可独立编译测试，见 `tests/parse_test.cpp` |
| `src/JsonScan.h` | 极简 JSON 取值（`FindMember` 只在本层找，会整段跳过嵌套值和字符串） | 不引第三方 JSON 库是有意的 |
| `src/Config.cpp` | ini 读写 + Key 来源解析链 | 配置目录来自宿主 `EI_CONFIG_DIR` |
| `src/OptionsDlg.cpp` | 原生 Win32 对话框（key / 间隔 / 代理 / 测试连接） | 所有文案在 `WM_INITDIALOG` 里设，`.rc` 只放布局（避免 rc.exe 代码页问题） |
| `src/TmCommon.h` | 字符串/时间/模块工具（`namespace tmu`） | 命名空间**不能叫 `tm`**，会和 `<ctime>` 的 `struct tm` 撞 |
| `src/TmSdk.h` | 包一层 `include/PluginInterface.h`，就地静音 API 头里的 C4100 | 保持接口头逐字节等于上游 |
| `include/PluginInterface.h` | TrafficMonitor 插件接口（**原样拷贝**，commit `188b877`，API v8） | 不要改它；源仓库：`D:\Projects\TrafficMonitor` |
| `tests/parse_test.cpp` | 解析层 58 个断言 | |
| `tests/host_sim.cpp` | 模拟宿主的最小测试程序 | 见"验证清单" |

## 4. 必须知道的实现约定

1. **配置必须立即落盘**：插件对象由宿主创建后**永不析构**，所以在析构函数里保存配置的写法
   （上游 `PluginDemo` 就是这么写的）永远不会执行。本插件在选项对话框点"确定"后立刻 `Save()`。
2. **模块 PIN**：宿主退出时会 `FreeLibrary` 插件 DLL，而后台线程可能还在跑 →
   `GetModuleHandleEx(...FROM_ADDRESS | ...PIN)` 把 DLL 钉住，避免线程踩到已卸载的代码页。
3. **`DataRequired()` 里绝不能发网络请求**：它是宿主监控线程每秒调的，卡住就是任务栏卡住。
4. **条目 ID 只用字母数字**（宿主把它写进 ini 持久化，改了等于丢用户配置）：
   `ocg5hused` `ocg5hleft` `ocgwused` `ocgwleft` `ocgmused` `ocgmleft` `ocgreset`
5. **`GetItemValueSampleText()` 决定任务栏预留宽度** → 百分比条目固定返回 `100%`，避免数字跳动时抖。
   倒计时条目返回 `23h59m`。
6. **`OnMouseEvent` 一律返回 0**（不吞事件）：左键点击时顺手触发刷新，但要让宿主继续处理，
   否则主窗口那条"点空白处拖动窗口"就废了。
7. `GetInfo()` 六项**都不许返回 nullptr**（空就返回 `L""`）；`GetItem`/`GetCommandName` 越界返回 `nullptr`。
8. 静态 CRT（`/MT`）、无 MFC、`/utf-8`；DLL 只依赖 `WINHTTP/SHELL32/ADVAPI32/USER32/KERNEL32`。
9. 编译产物：`ImportLibrary`/`ProgramDatabaseFile` 都指到 `obj\`，`plugins\` 目录里只该有 `.dll`。

## 5. 编译 / 部署

```bash
# 编译（输出直接写进 D:\Programs\TrafficMonitor\plugins\）
"C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe" \
  OpenCodeGoUsage.sln /p:Configuration=Release /p:Platform=x64

# 换部署目录
... /p:TrafficMonitorPluginDir=D:\别的路径\plugins
```

默认输出目录在 `OpenCodeGoUsage.vcxproj` 的 `TrafficMonitorPluginDir` 属性里（可被命令行覆盖）。

## 6. 验证清单

```bash
tests\build_and_run.bat     # 解析层单元测试，期望 "58/58 通过"
tests\build_host_sim.bat    # 编译 host_sim.exe
```

`host_sim.exe [dll路径] [配置目录] [观察秒数] [--dialog]` 会做宿主要做的事，逐项打印结果：

1. `LoadLibrary` + `TMPluginGetInstance` + `GetAPIVersion`
2. `GetInfo` 六项非空校验
3. `EI_CONFIG_DIR` 传递
4. 7 个条目枚举 + ID 字符集校验 + 越界索引返回 nullptr
5. 插件命令列表
6. **另一个线程每 200ms 调 `DataRequired`，主线程同时读值**（跑满观察期无崩溃 = 并发没问题）
7. tooltip 全文（按 `\r\n` 拆行打印）
8. `FreeLibrary` 不崩
9. `--dialog` 还会弹出选项对话框

按需配合：把 key 放进**进程环境变量**再启动 host_sim，就能验证网络路径而不把 key 写进任何文件：

```bash
export OPENCODE_API_KEY=<一个有效的 key>
cd /d/Projects/OpenCodeGoUsage/tests
cmd.exe //c "D:\\Projects\\OpenCodeGoUsage\\tests\\host_sim.exe" \
  "D:\\Programs\\TrafficMonitor\\plugins\\OpenCodeGoUsage.dll" \
  "D:\\Projects\\OpenCodeGoUsage\\obj\\testcfg\\" 8
```

**还没验证到的**：有效 key 的 200 响应在真实任务栏/主窗口里的显示。等用户换上新 key 后，
跑一次上面的命令应该看到 `12%` 之类的真实数字，并且 tooltip 里 `更新：` 有值。
真机验证：重启 TM → 任务栏右键 → 任务栏窗口设置 → 勾选条目；
也可以让 TM **退出**后直接改 `D:\Programs\TrafficMonitor\config.ini`：
`[task_bar] plugin_display_item` 是逗号分隔的条目 ID 列表，
`[plugin_display_str_taskbar_window]` 段可以给每个条目 ID 单独指定显示文本格式。
（TM 运行时改会被它在退出时覆写，所以必须先退出。）

## 7. 接口契约（别再重新调研）

```
GET https://opencode.ai/zen/go/v1/usage
Authorization: Bearer <sk-...>
Accept: application/json
User-Agent: TrafficMonitor-OpenCodeGoUsage/1.0
```

```jsonc
// 200
{"usage":{"rolling":{"status":"ok","percent":1,"resetsAt":"2026-08-15T12:34:47.397Z"},
          "weekly":{"status":"ok","percent":18,"resetsAt":"..."},
          "monthly":{"status":"ok","percent":11,"resetsAt":"..."}}}

// 错误（无 key 401 "Missing API key."；key 不存在 401 "Unauthorized"；工作区无订阅 403）
{"type":"error","error":{"type":"AuthError","message":"Unauthorized"}}
```

- `rolling` = 文档说的 **5 小时**窗口；`percent` 是**已用**整数百分比（不是余量）；`status` 只有 `ok` / `rate-limited`
- **没有美元额度**（限额是服务端 secret），所以"余量"只能算 `100 - percent`
- weekly 是自然周（周一 00:00 UTC 起），monthly 按**订阅创建日**锚定（不是自然月 1 号）
- 该接口**无公开文档**，是控制台前端在用的；改版即失效，失效时插件显示错误而不是错数据

## 8. 下一步可以做什么（按价值排序）

1. **补上 200 路径的真机验证**（等一个有效 key）——目前唯一的空白。
2. 任务栏进度条：`IPluginItem::IsDrawResourceUsageGraph()` 返回 1 + `GetResourceUsageGraphValue()` 返回 0~1，
   宿主会自己画占用条（类似 CPU 占用条）；目前刻意没做。
3. 条目自定义显示格式的文档化：宿主支持在 `[plugin_display_str_taskbar_window]` 里按条目 ID 定制文本，
   可以在 README 里给出示例。
4. 中英双语：`ITrafficMonitor::GetLanguageId()` 可用于切换字符串；目前只有中文。
5. 美元额度估算：**做不了**，除非 opencode 公开各模型限额（现在只有比例和百分比）。

## 9. 明确不要做的事

- 不要引入 MFC / 第三方 JSON 库 / 动态 CRT（本机没 MFC 组件；静态 CRT 是为了宿主目录零依赖）。
- 不要去读其它应用（OpenChamber / opencode 桌面版）的缓存或本地接口来"自动取 key"——
  用户明确否决过，而且那是脆弱的隐式依赖。Key 只来自：对话框 ini > 环境变量 > auth.json。
- 不要把轮询间隔默认值降到 30 秒以下；不要尝试绕过 `rate-limited`。
- 不要往 `D:\Projects\TrafficMonitor`（宿主的 fork）里写任何东西——本工程与它彻底解耦，
   只拷贝了一份 `PluginInterface.h`。
- 不要在日志/截图/文档里回显 API Key。
