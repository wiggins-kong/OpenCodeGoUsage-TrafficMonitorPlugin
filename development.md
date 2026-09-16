# 开发交接文档（development.md）

给下一个接手的人 / agent 看的：现在做到哪了、代码怎么组织的、怎么编译验证、
哪些坑已经踩过、下一步能做什么。

- **工程位置**：`D:\Projects\OpenCodeGoUsage\`（独立工程，**不在** TrafficMonitor 仓库里，fork 保持干净）
- **当前版本**：v1.2（2026-09-16）已发布（Key 入口改为 opencode.ai/auth、去掉 Key 的隐式来源，
  见第 8.2 节与 [changelog.md](changelog.md)）
- **部署目标**：`D:\Programs\TrafficMonitor\plugins\OpenCodeGoUsage.dll`
- **状态**：功能已完成并部署（v1.1 起含占用图，见"验证清单"）。曾经"没有有效 key"的空白已经补上：
  `plugins\OpenCodeGoUsage.ini` 里的 key 实测 **HTTP 200**（2026-09-16 复验拿到 rolling 6% / weekly 2% / monthly 1%），
  所以真实数据路径可以随时复验。

---

## 1. 一句话架构

纯 Win32 C++ 的 TrafficMonitor 插件 DLL（插件接口版本 8），后台线程每 60 秒 GET 一次
`https://opencode.ai/zen/go/v1/usage`，把三个窗口的百分比缓存成快照；
宿主的 `DataRequired()`（监控线程，每秒调）只读快照并格式化成 9 个显示条目的文本。

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
| 机器上有个失效的旧 Go key（明文躺在 `%APPDATA%\OpenChamber\Cache\Cache_Data\f_00001f`） | **已实测被服务端拒绝**（401 Unauthorized），别再拿它测。<br>**现在有效的 key 在 `D:\Programs\TrafficMonitor\plugins\OpenCodeGoUsage.ini`**（插件选项里填的那份），实测 200 |
| TM 运行时 `plugins\OpenCodeGoUsage.dll` 被映射，**无法覆盖** | MSBuild 报 `LNK1104: cannot open file ...`（bash 里 `cp` 报 "Device or resource busy"）→ 要么先退出 TM 再编译，要么 `-p:TrafficMonitorPluginDir=D:/.../obj/deploy` 编到别处 |

## 3. 文件地图

| 文件 | 职责 | 注意 |
|---|---|---|
| `src/Plugin.cpp` | `ITMPlugin` 实现、`TMPluginGetInstance` 导出、`DllMain`、9 个条目的构造、命令、tooltip 组装 | 插件对象是函数内静态对象，宿主永不析构它 |
| `src/UsageItem.cpp` | `IPluginItem` 实现，一个类靠构造参数区分 9 个条目（窗口 + 已用/剩余/倒计时）；占用图的值也在这里（`IsDrawResourceUsageGraph` / `GetResourceUsageGraphValue`） | 值文本在 `UpdateFrom`（监控线程）里写、`GetItemValueText`（UI 线程）里读 → 有 mutex，且返回用独立缓冲；占用图的值走同一把锁 |
| `src/GraphValue.h` | 百分比 → 占用图的值（纯 C++，`UsageItem.cpp` 和 `parse_test.cpp` 共用） | 那个 `+0.0005f` 不能省：宿主取 `int(value*100)` 截断，`n/100.0f` 在 53、59 上会算成 52.999996 |
| `src/UsageService.cpp` | WinHTTP 请求、后台线程循环、快照、代理探测、错误文案 | **接口 URL 在文件顶部的 `kPath` 常量**，接口变了改这里 |
| `src/UsageParse.cpp` | 响应体 → 结构体（纯 C++，不依赖 Windows） | 可独立编译测试，见 `tests/parse_test.cpp` |
| `src/JsonScan.h` | 极简 JSON 取值（`FindMember` 只在本层找，会整段跳过嵌套值和字符串） | 不引第三方 JSON 库是有意的 |
| `src/Config.cpp` | ini 读写 | 配置目录来自宿主 `EI_CONFIG_DIR` |
| `src/OptionsDlg.cpp` | 原生 Win32 对话框（key / 间隔 / 代理 / 测试连接） | 所有文案在 `WM_INITDIALOG` 里设，`.rc` 只放布局（避免 rc.exe 代码页问题）；Key 的 `ES_PASSWORD` 必须写在 `.rc` 里（见 8.2） |
| `src/TmCommon.h` | 字符串/时间/模块工具（`namespace tmu`） | 命名空间**不能叫 `tm`**，会和 `<ctime>` 的 `struct tm` 撞 |
| `src/TmSdk.h` | 包一层 `include/PluginInterface.h`，就地静音 API 头里的 C4100 | 保持接口头逐字节等于上游 |
| `include/PluginInterface.h` | TrafficMonitor 插件接口（**原样拷贝**，commit `188b877`，API v8） | 不要改它；源仓库：`D:\Projects\TrafficMonitor` |
| `tests/parse_test.cpp` | 纯逻辑单元测试 262 个断言（解析层 + 占用图取值 0~100 穷举） | |
| `tests/host_sim.cpp` | 模拟宿主的最小测试程序 | 见"验证清单" |

## 4. 必须知道的实现约定

1. **配置必须立即落盘**：插件对象由宿主创建后**永不析构**，所以在析构函数里保存配置的写法
   （上游 `PluginDemo` 就是这么写的）永远不会执行。本插件在选项对话框点"确定"后立刻 `Save()`。
2. **模块 PIN**：宿主退出时会 `FreeLibrary` 插件 DLL，而后台线程可能还在跑 →
   `GetModuleHandleEx(...FROM_ADDRESS | ...PIN)` 把 DLL 钉住，避免线程踩到已卸载的代码页。
3. **`DataRequired()` 里绝不能发网络请求**：它是宿主监控线程每秒调的，卡住就是任务栏卡住。
4. **条目 ID 只用字母数字**（宿主把它写进 ini 持久化，改了等于丢用户配置）。现在 9 个，按窗口分组：
   `ocg5hused` `ocg5hleft` `ocg5hreset` / `ocgwused` `ocgwleft` `ocgwreset` / `ocgmused` `ocgmleft` `ocgmreset`。
   v1.0 那个跨窗口取"最近一次重置"的 `ocgreset` 已经删掉（2026-09-15 改成按窗口区分），
   用户配置里若还留着这个 ID，宿主只会忽略它。
5. **`GetItemValueSampleText()` 决定任务栏预留宽度** → 百分比条目固定返回 `100%`，避免数字跳动时抖。
   倒计时条目返回各自窗口的最大值：5h 用 `4h59m`、周用 `6d23h`、月用 `30d23h`。
6. **`OnMouseEvent` 一律返回 0**（不吞事件）：左键点击时顺手触发刷新，但要让宿主继续处理，
   否则主窗口那条"点空白处拖动窗口"就废了。
7. **占用图是宿主画的，插件只给 0~1 的值**：`IsDrawResourceUsageGraph()` 返回 1 时，
   宿主用画 CPU 占用图的**同一段代码**（`TaskBarDlg.cpp:315-339` → `TryDrawStatusBar`）画
   虚线框 + 横向填充条，虚线框围的是**整个条目矩形**（含标签和文字）。因此：
   - 颜色 / 虚线框 / 柱状图-滚动图 全部由宿主的 `[task_bar]` 设置控制，插件改不了；
   - **只在任务栏窗口有**：主窗口（`SkinFile.cpp`）根本没有占用图这条路径，内置条目也没有；
   - 没有数据时 `IsDrawResourceUsageGraph()` 必须返回 0 —— 宿主对 0 值仍会画一个空虚线框；
   - 取值函数跑在 UI 线程（每次重绘都调），只能读缓存的快照，绝不能碰网络。
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
tests\build_and_run.bat     # 纯逻辑单元测试，期望 "262/262 通过"
tests\build_host_sim.bat    # 编译 host_sim.exe
```

`host_sim.exe [dll路径] [配置目录] [观察秒数] [--dialog]` 会做宿主要做的事，逐项打印结果：

1. `LoadLibrary` + `TMPluginGetInstance` + `GetAPIVersion`
2. `GetInfo` 六项非空校验
3. `EI_CONFIG_DIR` 传递
4. 9 个条目枚举 + ID 字符集校验 + 越界索引返回 nullptr
5. 插件命令列表
6. **另一个线程每 200ms 调 `DataRequired`，主线程同时读值**（跑满观察期无崩溃 = 并发没问题）
7. **占用图校验**：百分比条目"条的长度"必须等于文字里的数字；`--` 和倒计时条目必须返回 0
8. tooltip 全文（按 `\r\n` 拆行打印）
9. **倒计时条目校验**：`ocg5hreset`/`ocgwreset`/`ocgmreset` 的值必须分别等于 tooltip 里 5h/周/月
   三行末尾的 `（X 后）`（同一个快照算出来的，对不上就是条目读错了窗口）；没数据时两边都该是 `--`
10. `FreeLibrary` 不崩
11. `--dialog` 还会弹出选项对话框

两轮都要跑（插件配置目录 = `plugins\`，里面有真 key；坏 key 那轮的 ini 是临时造的）：

```bash
cd /d/Projects/OpenCodeGoUsage/tests

# 有数据：应当看到 "文字 8% / 条 8% OK" 之类
./host_sim.exe "D:\\Projects\\OpenCodeGoUsage\\obj\\deploy\\OpenCodeGoUsage.dll" \
               "D:\\Programs\\TrafficMonitor\\plugins" 8

# 没数据：9 个条目应当全部 "--" 且 "不画（符合预期）"
mkdir -p ../obj/badkey && printf '[config]\r\napi_key=sk-badkey-000\r\ninterval_sec=60\r\nproxy=\r\n' \
  > ../obj/badkey/OpenCodeGoUsage.ini
./host_sim.exe "D:\\Projects\\OpenCodeGoUsage\\obj\\deploy\\OpenCodeGoUsage.dll" \
               "D:\\Projects\\OpenCodeGoUsage\\obj\\badkey" 6
```

> 注意：**不要**用 `tests\build_host_sim.bat` 传带反斜杠的路径参数 —— cmd 会把引号吃掉，
> 表现成 `LoadLibrary 失败，GetLastError=126`（其实是路径没传对，不是 DLL 有问题）。

真机验证：重启 TM → 任务栏右键 → 任务栏窗口设置 → 勾选条目；
也可以让 TM **退出**后直接改 `D:\Programs\TrafficMonitor\config.ini`：
`[task_bar] plugin_display_item` 是逗号分隔的条目 ID 列表，
`[plugin_display_str_taskbar_window]` 段可以给每个条目 ID 单独指定显示文本格式。
（TM 运行时改会被它在退出时覆写，所以必须先退出。）

**部署的一个坑**：TM 运行时 `plugins\OpenCodeGoUsage.dll` 是被映射的，MSBuild 会报
`LNK1104: cannot open file ...`。要么先退出 TM 再编译（默认输出就写进 `plugins\`），
要么用 `-p:TrafficMonitorPluginDir=D:/.../obj/deploy`（注意用正斜杠，反斜杠会被 shell 吃掉）
编译到别处、再手工拷回去。TM 是提权运行的，关闭/重启这一步只能由用户做。

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

1. 条目自定义显示格式的文档化：宿主支持在 `[plugin_display_str_taskbar_window]` 里按条目 ID 定制文本，
   可以在 README 里给出示例。
2. 中英双语：`ITrafficMonitor::GetLanguageId()` 可用于切换字符串；目前只有中文。
3. 美元额度估算：**做不了**，除非 opencode 公开各模型限额（现在只有比例和百分比）。
4. 「已用」/「剩余」两套条目有点冗余（勾两套会得到方向相反的两条占用图）。如果哪天想砍掉
   「剩余」那三个，注意条目 ID 变了等于丢用户配置（`[task_bar] plugin_display_item` 里存的是 ID）。

## 8.1 占用图是怎么做的（2026-09-15 加的）

`IPluginItem::IsDrawResourceUsageGraph()` + `GetResourceUsageGraphValue()` 两个虚函数，
宿主的任务栏窗口会**自己**画虚线框和填充条，跟 CPU 占用图同一段代码（细节见第 4 节第 7 条）。
`IPluginItem::DrawItemEx()` / `IsCustomDraw()` 那条自绘路线**没有用**，不要再往那儿想：
一旦自绘，宿主那套「资源占用图 / 显示虚线框 / 占用图颜色 / 柱状图-滚动图」设置就全部失效了。

## 8.2 v1.2：Key 入口改为 opencode.ai/auth（2026-09-16）

opencode 的 Key 管理入口从 console 迁到 <https://opencode.ai/auth>（登录后进「API 密钥」页面），
本版把插件的入口和提示全部改过去，并去掉 Key 的隐式来源：

- `src/Plugin.cpp` 顶部的 `kConsoleUrl` 改为 `https://opencode.ai/auth`
  （「打开 OpenCode 控制台」命令用；菜单文字未改）。
- `OptionsDlg.cpp` 与 `UsageService.cpp` 里所有「opencode.ai/console 的 Keys 页面」
  文案都改成「opencode.ai/auth 登录后的「API 密钥」页面」。
- Key 只来自插件选项（ini）：删掉了 `Config::ResolveKey()` 及
  `KeyFromEnv` / `KeyFromAuthJson`，`Config.cpp` 不再需要 `JsonScan.h`。

### 踩到的坑：运行时补 `ES_PASSWORD` 不生效

v1.1 的写法是在 `WM_INITDIALOG` 里 `SetWindowLongPtrW(hEdit, GWL_STYLE, ... | ES_PASSWORD)`，
**看起来**有了遮罩，实际完全没生效 —— 截图里整串 Key 仍然明文。
edit 控件只在**创建时**读这个样式位，事后改 `GWL_STYLE` 不影响它的内部状态。

正确做法：`ES_PASSWORD` 写进 `res/OpenCodeGoUsage.rc` 的 `EDITTEXT` 样式列表（控件创建时就带上），
运行时只发 `EM_SETPASSWORDCHAR` 换遮罩字符（现在用的是 `L'\u25CF'` 圆点）再 `InvalidateRect`。
遮罩只影响显示，`GetWindowTextW` 依旧返回真实 Key，所以保存和「测试连接」都不用改。
**验证方式**：遮罩状态下点「测试连接」必须还能拿到 HTTP 200（已实测）。

### 顺带修掉的文案截断

`IDC_STATIC_HINT2` 原来只有 8 个单位高（一行），而那句提示在 300 单位宽里放不下 →
`res/OpenCodeGoUsage.rc` 里把它加高到 16（两行），下面几行顺次下移 8、结果框高度减 8。

不涉及条目 ID / 接口 / 数据路径；验证 = 单元测试 + `host_sim.exe` 三轮（真 Key / 坏 Key / 空 Key）
+ 选项对话框截图（有 Key 与空 Key 两种状态）。

## 9. 明确不要做的事

- 不要引入 MFC / 第三方 JSON 库 / 动态 CRT（本机没 MFC 组件；静态 CRT 是为了宿主目录零依赖）。
- 不要去读其它应用（OpenChamber / opencode 桌面版）的缓存或本地接口来"自动取 key"——
  用户明确否决过，而且那是脆弱的隐式依赖。Key 只来自插件选项（ini），
  环境变量 / auth.json 的自动回退也按用户要求删掉了。
- 不要在选项对话框里明文回显已保存的 Key。遮罩样式 `ES_PASSWORD` 要写在 `.rc` 里：
  运行时再补 `GWL_STYLE` 是**假生效**（看着加了，实际照样明文，v1.1 就这么错过一次）。
- 不要把轮询间隔默认值降到 30 秒以下；不要尝试绕过 `rate-limited`。
- 不要往 `D:\Projects\TrafficMonitor`（宿主的 fork）里写任何东西——本工程与它彻底解耦，
   只拷贝了一份 `PluginInterface.h`。
- 不要在日志/截图/文档里回显 API Key。
