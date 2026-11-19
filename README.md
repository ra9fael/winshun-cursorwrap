# ra9fael.cursorwrap — 多显示器光标回绕（Win顺 插件）

> **简体中文（默认）** · [English](README.en.md)

光标推到屏幕**外边**时，从**对边**重新出现。多显示器并排或上下叠放时，省掉横跨整个
桌面的长距离拖动。算法移植自 PowerToys CursorWrap（MIT）。

## 设计目的

多屏桌面上，把光标从最右边推到最左边要长距离拖动；很多工具用快捷键或热角（hot
corner）解决，都得额外记一个操作。本插件用的是「把光标推到屏幕边上」这个本来就会
做的动作：到了最外层的外边，就从布局的对边进来。

屏与屏**贴合**的那条边（内边）不参与回绕——那里 Windows 自己就会把光标送到相邻屏，
插件不插手，不会出现「闪回」。

## 用法

输入 `cursorwrap`（或 `wrap` / `回绕`）打开**精简的首页**：回绕状态、方向、撞击模式，以及调参窗口和数据窗口的入口。数值调参集中在调参窗口；`speed`、`band`、`tune` 等命令仍可直达。

| 行 | Enter |
|---|---|
| `回绕功能（已开启/已关闭）` | 启用 / 禁用回绕 |
| `回绕模式：<模式>` | 循环 双向 → 仅上下 → 仅左右 |
| `撞击模式：开/关` | 切换撞击模式 |
| `调参窗口` | 打开调参窗口（速度、加速度、测量带宽、防震、阈值、可视化、光效与建议值） |
| `数据窗口：开/关` | 子菜单：开关 / 拒绝原因 / 钩子健康 |
| `回绕功能：诊断信息` | 复制诊断全文 |

**子菜单**（每级第一行都是 `← 返回`；每行是一个可选值，Enter 直接应用）：

| 命令 | 子菜单 |
|---|---|
| `cursorwrap impact` / `撞击` | 返回 → 撞击开关 → 撞击速度 → 测量带宽 → 撞击加速度 |
| `cursorwrap speed` / `速度` | 返回 → 500/1000/2000/3000/5000/8000/12000/20000 px/s |
| `cursorwrap band` / `带宽` | 返回 → 50/80/120/150/200/300/500 px |
| `cursorwrap accel` / `加速度` | 返回 → 关闭 → 2000/5000/10000/20000/40000/80000 px/s2 |
| `cursorwrap mode` / `模式` | 返回 → 双向 → 仅上下 → 仅左右 |
| `cursorwrap activation` / `激活` | 返回 → 总是 → 按住 Ctrl → 按住 Shift |
| `cursorwrap guard` / `防震` | 返回 → 10/30/50/80/120/200 px |
| `cursorwrap edge` / `边缘` | 返回 → 1/2/5/10/20 px |
| `cursorwrap tol` / `容差` | 返回 → 10/30/50/80/120/200 px |
| `cursorwrap drag` / `拖拽` | 返回 → 开 → 关 |
| `cursorwrap single` / `单屏` | 返回 → 开 → 关 |
| `cursorwrap tune` / `调参` | 返回 → 开关 → 测量带宽 → 门槛 → 防震 → 阈值 |
| `cursorwrap data` / `数据` / `window` / `窗口` | 返回 → 开关 → 拒绝原因 → 钩子健康 → 复制轨迹 → 建议门槛 |
| `cursorwrap best` / `最优` | 返回 → 建议门槛 → 应用 → 查看数据 |

**命令先预览、Enter 才应用。** 直接输值不再立即改状态：

| 命令 | 效果 |
|---|---|
| `cursorwrap impact on` / `off` | 显示"撞击模式：开（待应用）"，Enter 应用 |
| `cursorwrap speed 3000` / `速度 3000` | 显示目标门槛（500–20000 px/s；应用时会一并打开撞击模式） |
| `cursorwrap band 300` / `带宽 300` | 测量带宽（50–500 px） |
| `cursorwrap mode vertical` / `模式 仅上下` | 回绕方向 |
| `cursorwrap ctrl` / `shift` / `always` / `按住` / `总是` | 激活方式 |
| `cursorwrap guard 120` / `防震 120` | 防震半径（10–200 px） |
| `cursorwrap edge 5` / `边缘 5` | 边缘判断阈值（1–20 px） |
| `cursorwrap tol 80` / `容差 80` | 邻接容差（10–200 px） |
| `cursorwrap taskbar` / `任务栏` | 返回 → -1 关闭 → 0 仅任务栏 → 20/48/50/100/200/500 px 邻近保护带 |
| `cursorwrap taskbar 0` / `任务栏 0` | 仅保护任务栏实际区域；`taskbar -1` 完全允许在任务栏附近回绕（范围 −1–500 px） |
| `cursorwrap drag on` / `off` | 拖拽时是否禁用 |
| `cursorwrap single on` / `off` | 单屏时是否禁用 |
| `cursorwrap tune on` / `off` / `调参 开` | 调参可视化开关 |
| `cursorwrap data on` / `off` / `数据 开` | 数据窗口开关 |
| `cursorwrap flash` / `光效` | 预览切换后的光效状态 |
| `cursorwrap trace` / `轨迹` | 复制轨迹行，Enter 复制 CSV |
| `cursorwrap best` / `最优` | 建议门槛：查看 / 应用 / 查看数据 |
| `cursorwrap clear` / `清除轨迹` | 预览清空动作 |
| `cursorwrap reset` / `恢复` | 预览恢复默认调参（宿主卡片的方向、激活等保持不动） |
| `cursorwrap diag` / `诊断` | 复制诊断行，Enter 复制全文 |

## 设置归属

- **宿主卡片**（Win顺 › 插件 › 回绕，存 Win顺 自己的配置）：回绕方向、激活方式、拖拽时禁用、单屏时禁用、撞击模式。
- **调参窗口**（设置卡上的"设置"按钮或搜索里的"调参窗口"行打开）：撞击速度、加速度、测量带宽、防震半径、邻接容差、边缘阈值、任务栏保护带（默认 0 = 仅任务栏区域，正值增加邻近范围，-1 = 关闭）、调参可视化、撞击光效、数据窗口和建议值。这些是插件自己的配置，保存在插件数据目录的 `settings.json`（只存与默认值不同的键），插件卡片改动与搜索命令都走同一条提交路径。

数值范围与算法含义不变；越界值回默认。升级后首次启动会把之前存在宿主里的调参值转存进插件自己的文件。

## 调参可视化

撞击参数不好凭感觉设——`cursorwrap tune` / `调参` 打开调参子菜单（开关 + 区间 /
门槛 / 防震 / 阈值），`tune on` / `tune off` 直接开关。开启后**只画外边缘，屏幕
中间完全不动**：

- **外边缘线**：每条能回绕的外边一条 2px 主题色实线——内边（两屏相接）不画，
  那里本来就不会回绕；
- **测速区间带**：线内侧 `impactBandPx` 宽的**半透明**条，区间大小一目了然；
- **速度读数**：撞到边缘时贴该边浮出 `实测 / 门槛 px/s` 面板，边框颜色
  绿 = 回绕、黄 = 速度不足被拦、灰 = 撞击关闭——你甩得到底多快直接照着设 `speed`；
  这个读数是**到达边缘那一刻**的速度，不是整段飞行的平均（详见下文）；
- **记录曲线**：主屏右下角的直方图面板，见下。

覆盖层不抢焦点、不进 Alt+Tab、点击穿透，颜色跟随宿主主题；面板按工作区定位，
不会被任务栏切掉。开关就是设置项 `tuningOverlay`，设置页与搜索栏同源，重启后仍然记住。

### 撞击门槛量的是什么

量的是**光标到达边缘那一刻的速度**。远处飞过来的高速段不计入——「甩过来但到边
已经慢下来」不会回绕，这正是「在边沿处加速才行」的意思。所以：

- 想要「甩一下回绕」，`speed` 设成比你贴边那一刻的手速略低；
- `加速度`（默认关闭）开启后**要求手在加速冲进边缘**：匀速滑到边缘再快也不回绕，
  到边缘已经减速的也不回绕，只有真正加速撞过去才回绕。建议先设 5000，觉得太难触发
  再降到 2000；读数面板会多一行 `accel 实测 / 门槛`（负值=正在减速）。测不到时
  面板标题直接给原因（折返 / 窗口太短 / 样本不足），数据窗里每条决策也带原因。
  手的 ±1px 抖动不会再把测量窗口打断（v0.7.0 在真机上几乎永远测不到，就是被
  这个抖动打断的）；
- `区间` 是测速的取样范围（默认 150px），是**硬限制**：区间外的高速飞行段不计入。
  区间内可测行程不足 20px 时测不出速度、不会回绕——这时把区间调大（粗采样或想
  更灵敏都可以试 200–300）；
- 读数不会出现天文数字：事件按驱动时间戳换算，排队投递只会让读数偏低（偏低就
  不触发，安全方向），并且有 20000 px/s 上限。

## 数据窗口

调参可视化画在边缘上，只在撞边那一刻有内容。**数据窗口**是可以一直摆在旁边的
那块：`cursorwrap data` / `数据`（或设置页的「数据窗口」开关）打开后，一个置顶、
**不抢焦点**、不进任务栏的小窗口每 150ms 刷新，显示

- 状态头：开关 / 钩子 / 屏数 / 外边缘数；
- 光标位置与**最近一次决策**（裁决 + 边 + 实测速度 + 加速度 + 当时门槛）；
- 到达速度直方图（对数横轴 500–20000，橙线当前门槛、绿线建议门槛，柱子按结果
  分色，刻度画在真实位置上）；
- **按原因分类的跳过计数**——`dragging` / `guard` / `not-on-edge` / `impact-gate` …
  「到底哪道门槛在拒」一行就能看出来；
- 最近 20 条决策日志（边 · 实测速度 · 加速度 · 裁决 · 屏号）。加速度测不到时
  写明原因（`n/a (reversed)` = 折返、`n/a (short)` = 窗口太短、`n/a (sparse)` =
  样本不足），不再只有孤零零一个 `n/a`。

调参可视化的 `edge arrivals` 直方图面板已并入这里——旧版是两份各自实现、横轴
还互相对不上的同一张图；边缘上保留的只有调参带、门槛线和撞边那一刻的读数。

点 X 关掉时设置项同步关掉，两个界面不会各说各话。它和调参可视化一样是设置项
`dataWindow`，设置页也能开。

## 记录曲线：怎么找到最优门槛

只给实时读数，你永远找不到最优值——它一次只回答一个问题。真正的问题是分布：
**「故意甩」产生的速度簇 和「顺路经过」产生的速度簇在哪里分开**。

所以插件每次到达外边都记一行（到达轨迹，最多 512 行）：

```
seq,t_us,monitor,edge,measured_pps,gate_pps,wrapped,gate_off
```

两点关键：

1. **被门槛拦下的也记**——只记通过的会让慢速簇永远缺席，直方图只有一半，
   建议值无从算起；
2. **一次到达只记一行**——手停在边缘时 Windows 钳位指针，会产生成百上千个
   同坐标样本，它们属于同一次到达。

调参数据窗里的直方图画的就是这些行（对数横轴 500…20000 px/s），橙色竖线是**当前
门槛**，绿色竖线是**建议门槛**；柱子按结果分色（绿色顶 = 当时真的回绕了，灰顶 =
被挡下），标题里同时给「当时回绕数」和「按当前门槛会有几条过」两个数——旧版把
预测值标成 "wrap"，读起来像"低速也穿越了"。

`cursorwrap best` / `最优` 给出建议（`adviseGate`，纯函数）：取相邻速度之间**最大
的倍数空档**（≥ 1.5×才算两个簇；700→1000 这种 25% 的小台阶是噪声——旧版按相对
间隔算，恰好偏向低速端，才有了"gate 3000 建议 908"），门槛落在空档的几何中点；
两侧各须占至少 1/6 样本，单个离群值成不了簇。当前门槛**已经落在空档里**时直接说
「当前门槛合适」，不再给第二个数字。空档不够宽就明说「数据不足」，**不编造数字**。
点「采用这个门槛」即写入并立即生效。

**到达轨迹只描述当前门槛**：改门槛 / 区间 / 加速度门槛会清空重新记录——旧门槛下
记的行对新门槛毫无意义，混在一起正是建议值和当前设置打架的根源。

`cursorwrap trace` / `轨迹` 把 CSV 复制到剪贴板，可离线画图核对；`clear` /
`清除轨迹` 清空重记。

**顺带的丝滑度修复**：回绕落点从"对边边缘线"改为**内缩 1px**——落点本身就在
判定带上会让防震圈被迫吞掉一次触发；同时修掉了「贴着边缘继续推会把测得速度稀释
到 0」的缺陷（钳位样本不再计入平均速度），这是之前"完全失效"的真正原因。

## 设置

这些设置在宿主卡片上（Win顺 › 插件 › 回绕），由 Win顺 保存：

| 键 | 默认 | 说明 |
|---|---|---|
| `wrapMode` | both | 双向 / 仅上下 / 仅左右 |
| `activationMode` | always | 总是回绕 / 按住 Ctrl / 按住 Shift |
| `disableDuringDrag` | true | 按住左键拖动时暂不回绕 |
| `disableOnSingleMonitor` | false | 只有一个屏时禁用（默认仍回绕到同一屏的对边） |
| `impactGate` | false | 撞击模式：需要足够速度撞上边缘才回绕，慢慢挪过去不触发 |

以下设置在**调参窗口**里（设置卡的"设置"按钮，或搜索里的"调参窗口"行），由插件保存在自己数据目录的 `settings.json`（只存与默认值不同的键）：

| 键 | 默认 | 说明 |
|---|---|---|
| `impactSpeedPps` | 2000 | 500–20000，撞击速度门槛（px/s），取最近一次连续行程的平均速度 |
| `impactBandPx` | 400 | 50–500，测量带宽：只在距边缘这段范围内测速，远处冲刺不算 |
| `impactAccelPps2` | 0 | 0 = 关闭（只看速度）；开启时手必须在边缘前仍在加速，可用下限 200 |
| `wrapGuardPx` | 50 | 10–200，回绕后此范围内的防振半径 |
| `adjacencyTolerancePx` | 50 | 10–200，相邻边缘的判断容差（内边不回绕） |
| `edgeThresholdPx` | 1 | 1–20，离边缘多近算"在边缘上" |
| `tuningOverlay` | false | 调参可视化：绘制测量带、回绕线和实测速度 |
| `impactFlash` | false | 撞击光效：成功撞击回绕时边缘亮一下 |
| `dataWindow` | false | 数据窗口：实时直方图、建议门槛和拒绝原因 |

两个窗口开关（可视化 / 数据窗口）关闭时即停，搜索页和调参窗口改的是**同一批字段**——搜索命令、调参窗口控件和建议值都走同一条提交路径，改单项不会重置其他参数。

越界值一律回到默认，不报错。

## 权限说明

本插件只申请 `ui`（窗口与钩子）权限，因为需要：

- 一个低级鼠标钩子（`WH_MOUSE_LL`）来观察光标移动；
- 一个**不可见的** message-only 窗口来接收显示器变更通知。

它不申请管理员权限、不访问网络、不读写文件、不动剪贴板。光标坐标与显示器几何
**只在插件进程内使用**，不落盘、不出网。

宿主对 `ui` 只是**部分**强制，这一点请按你自己的判断接受或拒绝。

## 已知限制

- 独占全屏（exclusive fullscreen）的 DirectX 程序里外框可能不生效
- 若系统因 `LowLevelHooksTimeout` 把钩子摘掉，本版有**看门狗**：每秒比对真实光标
  位置与钩子最后报告的位置，连续两个周期「光标动了但没有事件」就判定掉线并重装，
  `cursorwrap diag` 里的 `hook reinstalls` / `hook responsive` 会记下来
- **远程桌面（RDP）支持**：会话里只有一块虚拟屏，走单屏语义——左右边缘照样回绕，
  落点为对侧内缩 1px（右边缘 → `x=1`，左边缘 → `x=1918`）。状态行会标出
  `远程桌面`，并在「单屏时禁用」把它关掉时明确写出
  `disabled on one monitor`。断开重连时的 `WM_DISPLAYCHANGE` 不会再清空显示器拓扑
  （空枚举会被拒绝并计入 `rebuildRefused`）
- 高对比度、放大镜等辅助功能未专门验证
- 与 PowerToys 原版的差异：没有「快捷键触发回绕」模式，也没有按进程自动启停

## 安装

1. 从 [Release](https://github.com/ra9fael/winshun-cursorwrap/releases/latest) 下载
   `ra9fael.cursorwrap-<version>.wsplugin`（可用旁边的 `.sha256` 核对：
   `Get-FileHash <文件> -Algorithm SHA256`）
2. Win顺 → **设置 → 插件 → 已安装 → 添加插件 → 压缩包**，选中该文件
3. 本地安装的插件**不会自动启用**，请在列表里打开开关；启用时确认 `ui` 权限

本插件是原生 exe 包，需要 Win顺 0.22.0 或更新（清单的 `minWinShun` 声明了这一点）。
Win顺 认出同一个 id 会询问是否替换旧版本，并保留插件的数据目录；卸载在
**设置 → 插件 → 已安装** 里移除。

**升级后需重新启动插件进程**（停用再启用，或重启 Win顺）——原生 exe 不走脚本热重载。

**手动安装**（不经过宿主界面）：把包内文件解到
`%LOCALAPPDATA%\WinShun\plugins\ra9fael.cursorwrap\`（`plugin.json` 必须在目录根部），
再在 `WinShun.ini` 加：

```ini
[Plugin.ra9fael.cursorwrap]
Enabled=true
Confirmed=ui
```

然后重启 Win顺。`Confirmed=ui` 表示你确认了 `ui` 权限；没有这一行，宿主会在启用时
弹确认。

## 从源码构建

```powershell
$qtKit = 'C:/Qt/6.12.0/msvc2022_64'
cmake -S . -B build/local -G Ninja -DCMAKE_BUILD_TYPE=Release "-DCMAKE_PREFIX_PATH=$qtKit"
cmake --build build/local
$env:PATH = "$qtKit/bin;$env:PATH"
ctest --test-dir build/local --output-on-failure
```

需要 CMake 3.21+、x64 MSVC 和 **Qt 6.12.0 MSVC 开发套件**（含 Qt Quick 和 Qt Test）。
构建生成 `build/local/plugin/Release/cursorwrap.exe` 与同目录下的 `CursorWrapApp.dll`。
安装需使用内置 Qt 6.12 的 Win顺，插件通过清单的 `qt: host` / `qtVersion: 6.12` 契约共享运行时。
只构建引擎和 devshell 时可配置 `-DCW_BUILD_PLUGIN=OFF`（支持 MinGW-w64）。

## 测试

```powershell
ctest --test-dir build/local --output-on-failure       # L1：140 用例 + 6 布局 fixture
python tests/protocol_roundtrip.py --exe build/local/plugin/Release/cursorwrap.exe --app-dir 'C:/Program Files/WinShun'               # L3：119 项协议往返检查
python tests/devshell_scenarios.py               # L3.5：免宿主 GUI + 逻辑场景
python tests/repeat_push_probe.py                # L4 单屏 / 远程桌面：真实 SetCursorPos 多轮推送
python tests/injected_wrap_probe.py              # L4：注入事件（LLMHF_INJECTED）仍能回绕
python tests/validate.py                         # L0：清单/双语文档/打包静态校验
```

> `tests/live_wrap_probe.py` 的判据（光标位置 ≠ 目标）不成立——被边缘夹住也算「变了」。
> 它的结论已撤回，请用 `repeat_push_probe.py`（以插件计数器 `delta_wraps` 为判据）。

### 不开宿主、不动鼠标就能测 GUI

`build/cw_devshell.exe` 链接的是插件发布的**同一批源文件**（引擎、遮罩、数据窗口），
只把输入换成脚本化的合成样本，所以**不会移动真实光标**——远程桌面下
`SetCursorPos` 会失败、你自己动一下鼠标又会作废整轮，这个壳子都绕开了。

```powershell
cmake --build build --target cw_devshell
build\cw_devshell.exe diag                       # 真实布局与外边缘
build\cw_devshell.exe gui                        # 真窗口，键盘 1 慢漂 / 2 快甩 / 3 刹车 / q 退出
build\cw_devshell.exe run tests\scenarios\impact-speed.txt
tools\devshell\capture.ps1 -Scenario tests\scenarios\visual-demo.txt -Out shot.png
```

场景文件见 `tests/scenarios/`，一行一条命令（`set` / `move` / `stroke` / `brake` /
`expect wrap|no-wrap|speed>=N` …）。

设计说明见 [docs/spec.md](docs/spec.md)，测试规格见 [docs/tests.md](docs/tests.md)，
本轮验证证据见 [docs/validation.md](docs/validation.md)。

## 打包

```powershell
python tools/package.py --bin-dir build/local/plugin/Release # dist/<id>-<version>.wsplugin + .sha256 + release-notes.md
python tools/package.py --bin-dir build/local/plugin/Release --tag v1.2.0   # 校验 tag 与 plugin.json 版本一致、CHANGELOG 有对应 section
```

包内容：Git 跟踪的文件与 `cursorwrap.exe`、`CursorWrapApp.dll`，确定性 zip（固定时间戳、不压缩），
同一份输入永远得到同一个 SHA-256。发布流程（tag → CI verify/build/publish）见
[.github/workflows/release.yml](.github/workflows/release.yml) 与
[AGENTS.md](AGENTS.md)。双语 release notes 在
[docs/release-notes/v0.1.0.md](docs/release-notes/v0.1.0.md)。

## 许可证

MIT。算法移植自 PowerToys CursorWrap（Microsoft Corporation，MIT），
详见 [docs/spec.md](docs/spec.md) 第 5 节。

GUI 图标由 [winshun-plugin-icons](https://github.com/ra9fael/winshun-plugin-icons)
从 Phosphor 字形 `arrows-out-cardinal` 生成。
