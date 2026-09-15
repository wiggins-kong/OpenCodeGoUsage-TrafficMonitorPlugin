# OpenCode Go 用量 —— TrafficMonitor 插件

在 TrafficMonitor 的任务栏窗口（或主窗口）里实时显示 [OpenCode Go](https://opencode.ai/docs/go/) 订阅的
**5 小时 / 每周 / 每月** 用量与余量。

## 它显示什么

数据来自 opencode 控制台前端自己用的用量接口：

```
GET https://opencode.ai/zen/go/v1/usage
Authorization: Bearer <Go 的 API Key，sk- 开头>
-> {"usage":{"rolling":{"status":"ok","percent":1,"resetsAt":"…"},
             "weekly":{...},"monthly":{...}}}
```

接口里的 `rolling` 就是文档说的 5 小时窗口；`percent` 是**已用**百分比（整数）。
**接口不提供美元额度**，所以“剩余”只能按 `100 - 已用` 算，做不出“还剩 $23.40”这种显示。

插件提供 9 个显示条目（每个窗口三条），在 TrafficMonitor 的“任务栏窗口设置”里按需勾选：

| 条目 | 标签 | 值示例 | 含义 |
|---|---|---|---|
| 5小时已用 | `5h` | `12%` | 5 小时滚动窗口已用百分比 |
| 5小时剩余 | `5h余` | `88%` | 100 − 已用 |
| 5小时重置 | `5h重` | `2h13m` | 距离**5 小时窗口**下一次重置的倒计时 |
| 本周已用 | `周` | `18%` | 本周（周一 00:00 UTC 起）已用 |
| 本周剩余 | `周余` | `82%` | |
| 本周重置 | `周重` | `5d3h` | 距离**本周**重置的倒计时 |
| 本月已用 | `月` | `11%` | 本月（按订阅创建日锚定）已用 |
| 本月剩余 | `月余` | `89%` | |
| 本月重置 | `月重` | `12d7h` | 距离**本月**重置的倒计时 |

三个倒计时各看各的窗口，**不是**“最近的一次重置”：`5h重` 是 5 小时窗口的刷新倒计时
（配额用完后最先恢复的就是它），`周重` / `月重` 分别是本周、本月的重置倒计时。
超过一天显示成 `天+小时`（`5d3h`），一天以内是 `小时+分钟`（`2h13m`），不到一分钟是 `<1m`；
那个窗口的重置时刻还没拿到（比如数据没成功过）时显示 `--`。

任务栏里最终显示成 `5h 12%` 这种形式；分隔符、是否显示标签、对齐、颜色都由 TrafficMonitor
自己的设置控制，插件不干预。

## 占用图（虚线框 + 填充条）

6 个百分比条目在**任务栏窗口**里会带一条占用图 —— 跟 CPU / 内存条目那个样子完全一样：
整个条目套一个虚线框、里面按百分比填充一条色带、文字叠在色带上面。
**三个重置倒计时**条目不画（它们显示的是时间，没有百分比）。

这不是插件自己画的：插件只按接口约定给出 0~1 的值，虚线框和填充条由 TrafficMonitor 画
（跟画 CPU 占用图是同一段代码），所以**样式全部听 TrafficMonitor 的**：

| 想要的效果 | 去哪儿改（右键任务栏窗口 → 任务栏窗口设置） |
|---|---|
| 开/关占用图 | 勾选「显示资源占用图」（管住该窗口里所有条目，插件不提供单独的开关） |
| 虚线框 | 勾选「显示虚线框」 |
| 柱状图 / 横向滚动图 | 「占用图类型」单选 |
| 颜色 | 「占用图颜色」或「跟随 Windows 主题颜色」 |

几条容易踩的：

- **条的长度 = 该条目文字里的数字**。`5h 8%` 的条填 8%，`5h余 92%` 的条填 92%（"剩余"条目画的
  就是剩余量，不是占用），这样文字和条永远一致。
- **只在任务栏窗口有**。TrafficMonitor 的主窗口（悬浮窗）不画占用图，它的内置条目也一样。
- **没有数据时不画**（条目显示 `--` 时既不画条也不画虚线框）—— 宿主的规矩是值为 0 也要画一个
  空虚线框，插件刻意用返回值把它挡掉了。
- 「横向滚动图」也能用，但用量是 **60 秒**才变一次的数据，而宿主的滚动图是按重绘采样的，
  看起来会是一条几乎静止的色带。想看"用了多少"用**柱状图**。

## 安装

1. **编译**（需要 VS2022 + C++ 工作负载；本工程**不需要 MFC 组件**）：

   ```
   msbuild OpenCodeGoUsage.sln /p:Configuration=Release /p:Platform=x64
   ```

   也可以直接用 VS 打开 `OpenCodeGoUsage.sln` 编译。
   产物 `OpenCodeGoUsage.dll` 会**直接写进** `D:\Programs\TrafficMonitor\plugins\`。
   换地方/换机器时改 `OpenCodeGoUsage.vcxproj` 里的 `TrafficMonitorPluginDir`，或命令行传
   `/p:TrafficMonitorPluginDir=...`。

   > DLL 位数必须和 TrafficMonitor 一致：宿主是 x64 就编 x64，x86 宿主编 Win32。
   > 位数不匹配时宿主只会报“插件模块加载失败”。

2. **重启 TrafficMonitor**（插件只在启动时扫描 `plugins` 目录）。

3. 右键任务栏窗口 → “任务栏窗口设置” → 勾选想要的条目；
   在“其他功能 → 插件管理”里可以确认插件已加载、看插件详情。

## 配置 API Key

按优先级从三处取，取到第一个非空的就用：

1. **插件选项里填的**（右键插件条目 → “插件选项”，或插件管理对话框里的“插件选项”）
   → 存到 `<插件配置目录>\OpenCodeGoUsage.ini`（便携模式下就是 `plugins\OpenCodeGoUsage.ini`）
2. 环境变量 `OPENCODE_API_KEY`
3. `%USERPROFILE%\.local\share\opencode\auth.json` 里 `opencode-go.key`（装了 opencode CLI 才会有）

Key 在 <https://opencode.ai/console> 的 **Keys** 页面创建。
选项对话框里的 **「测试连接」** 会立刻用输入框里当前的值请求一次并显示原始结果（不会保存），
换机器排查时很好用。

## 交互

| 操作 | 效果 |
|---|---|
| 左键点条目 | 立即刷新（不吞掉事件，主窗口拖拽、双击动作照常） |
| 右键 → 插件命令 | `立即刷新` / `打开 OpenCode 控制台` |
| 鼠标悬停 | 完整状态：三个窗口的已用/余量/重置时刻/倒计时、Key 来源、实际走的代理、上次更新时间、错误原因 |

## 刷新与容错

- 后台线程每 **60 秒**拉一次（选项里可改 30–3600）；宿主每秒调用的 `DataRequired()` 只读缓存，
  **绝不发网络请求**，任务栏不会卡。
- 请求失败时**保留上次的数值**（网络抖一下不至于把任务栏变成 `--`）；从未成功过才显示 `--`。
- 错误原文（如 `401 AuthError: Unauthorized`）会出现在 tooltip 和「测试连接」的结果里。
- 代理：默认自动（环境变量 `HTTPS_PROXY`/`HTTP_PROXY` → 系统代理 → 直连）；
  也可以在选项里填 `host:port` 或 `direct`。实际用了哪个会写在 tooltip 里。

## 故障排查

| tooltip 里的提示 | 含义 / 怎么办 |
|---|---|
| `401 AuthError：Unauthorized` | Key 无效或已被删除 → 去 console 的 Keys 页面重新生成一个 |
| `403 EntitlementError` | 这个 Key 所在的工作区没有 OpenCode Go 订阅 |
| `无法连接 opencode.ai` | 需要走代理 → 在插件选项的“代理”里填 `127.0.0.1:端口` |
| `DNS 解析失败` | 同上，或网络本身有问题 |
| `未配置 API Key` | 三处都没有 Key → 在插件选项里填一个 |
| 条目一直显示 `--` | 一次都没成功过，看 tooltip 里的错误行 |

## 代码结构

| 文件 | 作用 |
|---|---|
| `src/Plugin.cpp` | `ITMPlugin` 实现、`TMPluginGetInstance` 导出、插件信息、命令、tooltip 组装 |
| `src/UsageItem.cpp` | `IPluginItem` 实现（9 个条目共用一份代码，构造参数区分：窗口 + 已用/剩余/倒计时） |
| `src/UsageService.cpp` | WinHTTP 请求、后台轮询线程、快照缓存、代理探测 |
| `src/UsageParse.cpp` | 响应解析（纯 C++，不依赖 Windows，可独立测试） |
| `src/GraphValue.h` | 百分比 → 占用图的值（纯 C++；宿主的 `int(value*100)` 截断会让朴素的 `n/100.0f` 在 53、59 上少 1） |
| `src/JsonScan.h` | 极简 JSON 取值（不引第三方库，只读需要的几个字段） |
| `src/Config.cpp` | ini 读写 + Key 来源解析链 |
| `src/OptionsDlg.cpp` | 原生 Win32 选项对话框 |
| `include/PluginInterface.h` | TrafficMonitor 插件接口头（原样拷贝，见下） |

几个刻意的设计选择：

- **纯 Win32 + WinHTTP，静态 CRT**：DLL 只依赖 WINHTTP/SHELL32/ADVAPI32/USER32/KERNEL32，
  宿主目录里不需要附带任何运行库，也不要求 VS 装 MFC 组件。
- **模块 PIN**：宿主退出时会 `FreeLibrary` 插件，而后台线程可能还在跑，
  所以用 `GetModuleHandleEx(...PIN)` 把 DLL 钉住，避免线程踩到已卸载的代码页。
- **配置立即落盘**：插件对象由宿主创建后永不析构（接口约定），任何“在析构函数里保存配置”
  的写法都不会执行，所以 Save 发生在选项对话框点“确定”之后。

## 测试

```
tests\build_and_run.bat      纯逻辑单元测试（262 个断言：解析层 + 占用图取值 0~100 穷举）
tests\build_host_sim.bat     编译并运行 host_sim.exe —— 模拟宿主加载插件
```

`host_sim.exe [dll路径] [配置目录] [秒数] [--dialog]` 会做真实宿主要做的事：
`LoadLibrary` → `TMPluginGetInstance` → 校验 `GetInfo`/条目 ID → 传 `EI_CONFIG_DIR` →
在**另一个线程**里反复调用 `DataRequired` 同时主线程读值（用来暴露数据竞争）→
校验占用图的返回值与条目文字一致 → 校验三个重置倒计时条目各自和 tooltip 里同一窗口的倒计时对得上
（条目取错窗口时会立刻报错）→ 打印 tooltip。
加 `--dialog` 会把插件选项对话框弹出来。

配置目录里放一份坏 key 的 `OpenCodeGoUsage.ini` 再跑一次，就能验证"没数据时不画占用图"：
9 个条目应该全部显示 `--` 且 `IsDrawResourceUsageGraph()` 返回 0。

## 已知限制

- 只做 OpenCode Go 三个窗口。**不做**美元金额估算、Zen 计费余额、Claude/Codex 等其它厂商额度、多账号。
- 该用量接口**没有公开文档**（是控制台前端在用的接口），opencode 改版就可能失效。
  失效时插件会显示错误而不是静默显示错数据；接口地址是 `src/UsageService.cpp` 顶部的 `kPath` 常量，改一行即可。
- 无论 key 走哪条来源，轮询的都是**你自己账号**的用量，默认 60 秒一次，并带自己的 User-Agent；
  若某个窗口已被限流，插件只在 tooltip 里标注，不会尝试绕过。
- 界面只有中文。

## 另见

- [changelog.md](changelog.md) —— 更新日志（当前 v1.0）
- [development.md](development.md) —— 开发交接文档：架构、环境约束、验证清单、
  接口契约、下一步可做的事、明确不要做的事

## 出处与许可

- `include/PluginInterface.h` 拷贝自 [zhongyang219/TrafficMonitor](https://github.com/zhongyang219/TrafficMonitor)
  （commit `188b877`，插件接口版本 8），版权归原作者，本工程原样使用未做修改。
- 其余代码为本工程所有。
