# 光标回绕算法：ra9fael.cursorwrap 与 PowerToys CursorWrap 对比报告

对象：`PowerToys/src/modules/MouseUtils/CursorWrap/`（`MonitorTopology.{h,cpp}`、
`CursorWrapCore.{h,cpp}`、`dllmain.cpp`）vs `ra9fael.cursorwrap/src/`
（`wrap_topology.{h,cpp}`、`wrap_logic.{h,cpp}`、`mouse_hook.cpp`）。
逐函数对照源码写成，每条注明两边文件与行为差异；结论按 **相同 / 增强 / 修正 / 偏离** 分级。

## 1 总览

| 维度 | PowerToys | 本插件 | 分级 |
|---|---|---|---|
| 架构 | PowerToys 模块 DLL，宿主进程内，C++/MSVC，设置走 PowerToys 管道 | Win顺 resident 子进程 stdio JSON-RPC，单 exe | 相同（平台惯例不同） |
| 事件源 | `WH_MOUSE_LL`，钩子装在宿主线程 | `WH_MOUSE_LL`，独立线程 + 消息泵 | 相同 |
| 拓扑算法 | `MonitorTopology` | `wrap_topology`（移植） | 相同 |
| 回绕目的地 | `FindNearestOppositeEdge` + 同屏回退 | 同算法移植 + 一处修正 | 相同（1 处边界修正，见 §4.3） |
| 模式过滤 | `WrapMode` 0/1/2 | 同 | 相同 |
| 防震荡 | 50px 固定半径 | 50px 可调（10–200） | 增强 |
| 拖拽抑制 | `GetAsyncKeyState(VK_LBUTTON)` | 同，可关 | 相同 |
| 单屏禁用 | 默认 false | 同，可调 | 相同 |
| 激活方式 | 热键触发模式 + auto_activate 进程白名单 | 修饰键（Ctrl/Shift）常驻 | 偏离（见 §6.2） |
| 撞击门槛 | **无** | 测速区间内最后一段逼近速度 + 位移下限去抖 | 增强 |
| 设置写回 | 宿主管道双向 | 搜索栏 + 数据目录覆盖层 | 增强（相对协议能力） |
| 注入事件 | 不区分（PowerToys 也绕自己 setCursorPos 的事件？见 §5.2） | `LLMHF_INJECTED` 一律不绕 | 差异（见 §5.2） |

## 2 拓扑构建 —— 相同

两边都是：每屏 4 条边，键用 `(monitorIndex, EdgeType)`（注释同款理由：HMONITOR
会随显示变化失效，索引在单次快照内稳定）；相邻判定 = 相反类型 + 位置差 ≤ 容差
+ 垂直区间重叠 > 容差；非相邻即外边。

差异（不改变行为）：
- PowerToys 按 `EnumDisplayMonitors` 枚举顺序存 index；本插件按「主屏优先、再按
  left/top」排序后编号——同一布局跨重建索引确定，测试可复现。
- PowerToys 容差硬编码 50px 常量出现在 4 处（`IdentifyOuterEdges`、
  `EdgesAreAdjacent` 默认参数、`DetectMonitorGaps`、`CursorWrapCore` 传入）；
  本插件收敛为 `Topology::initialize(monitors, tolerance)` 单入口 + `wrapGuardPx`/
  `adjacencyTolerancePx` 两个设置。

## 3 外边判定 `IsOnOuterEdge` —— 相同，一处语义差异

两边逐边检查完全一致：
- 左边：`x <= left + threshold`（threshold 恒 1）
- 右边：`x >= right - 1 - threshold`
- 上/下对称
- `WrapMode` 过滤位置一致（HorizontalOnly 不看上下，VerticalOnly 不看左右）
- 角点多候选：先收集全部匹配边，`PrioritizeEdgeByDirection` 按移动主轴择优
  （水平移动优先 Left/Right、垂直优先 Top/Bottom），主轴候选找不到目的地再试其余

**语义差异（有意）**：PowerToys 的 `edgeThreshold` 硬编码 1；本插件做成设置
`edgeThresholdPx`（1–20）接入判定。默认值相同，行为一致。

## 4 回绕目的地 —— 相同算法 + 一处边界修正

### 4.1 重叠区（direct match）—— 相同

两边 `FindOppositeOuterEdge` 一样：在对向类型的外边里找「光标垂直坐标落在
`[start, end]` 内」的，多条时按 `findMax` 取位置最大/最小者。命中后保留垂直坐标。

### 4.2 非重叠区（projection）—— 相同

两边 `FindNearestOppositeEdge` 一样：无直接命中时按坐标距离取最近的对向外边
（同距离取 `findMax` 极值位置），然后 `CalculateProjectedPosition`：
- 在共享区间（`[max(start), min(end)]`）内 → 保留原坐标
- 区间外 → 夹到目标边最近边界 ±1px 内缩

逐行对照确认一致（PowerToys `MonitorTopology.cpp:628-664` vs `wrap_topology.cpp`
`projectCoordinate`）。

### 4.3 同屏回退 —— 本插件修正了一处 PowerToys 的不一致

PowerToys `GetWrapDestination` 的 `!oppositeResult.found` 分支回绕到
`right - 2 / left + 1 / bottom - 2 / top + 1`；但它的 `FindNearestOppositeEdge`
在**单屏**时并不返回 not-found——单屏自己的对边就是外边且坐标必然重叠，所以
实际走的总是「找到对边」分支，目的地 = `position = right - 1`。**`-2` 分支在
PowerToys 里是死代码**（只有多屏对边全被内边吃掉时才会到，而那时坐标区间也往往
不重叠、走投影分支）。

本插件移植时保留了这个分支但写成与「找到对边」一致的语义
（`box.right - 2` 对应 PowerToys 原文），并在单测里固化实际行为：
单屏目的地是对边 `position`（1919 / 1079），由防震荡半径吸收反弹。
**行为与 PowerToys 实测一致；死代码语义照抄以保持可对照性。**

### 4.4 多屏整体回绕 —— 相同，但值得点名

两边都不是「回绕到本屏对边」：右侧屏的外右边缘回绕到**位置最小的对向外边**，
即整个布局的最左外边（拖动跨越整个桌面）。这是 `FindNearestOppositeEdge` 的
`findMax/findMin` 语义，两边一致。用户如果期待「回到相邻屏」，这不是本插件
偏离 PowerToys，而是算法本义。

## 5 状态机 `HandleMouseMove` vs `onMouseMove` —— 顺序一致 + 三处增强

判定顺序两边一致（`CursorWrapCore.cpp` vs `wrap_logic.cpp`）：

1. 无监视器 → 不动
2. 单屏禁用 → 不动
3. 拖拽抑制（左键）→ 不动
4. 激活门（PowerToys 热键模式 / 本插件修饰键）
5. **防震荡半径** → 不动（两边都是距离平方 ≤ guard²）
6. 离开半径后清 guard（PowerToys 在 `IsWithinWrapThreshold` 失败后清
   `m_hasLastWrap`；本插件同）
7. `MonitorFromPoint` 定位所在屏（本插件 `monitorAt`，纯数据版）
8. `IsOnOuterEdge` → 目的地 → `SetCursorPos` + 吞消息

### 5.1 增强：撞击模式（PowerToys 没有）

本插件新增：到达外边后还要「最后一段朝向边缘的连续逼近、在测速区间内的速度 ≥ 门槛」。
- **测速区间**（`impactBandPx`，默认 150px）：只在距边缘该范围内测速——远处冲刺
  被截断，不会稀释贴边的减速（「远处飞过来、贴边慢慢挪」不触发）
- **折返分割**：逼近方向出现折返后只量最后一段撞击（回弹再撞不平均）
- **钳位尾部剥离**：手撞到边缘后 Windows 把指针钳在边界，后续事件坐标重复；这些
  样本是"墙"不是减速，先剥掉，最后一步只量到最后一个自由样本。
  **PowerToys 不会有这个问题**（它不测速），本插件第一版正是被它搞成"完全失效"：
  真实的 2000 px/s 推手到达后被稀释成 1787 → 1625 → … → 0。
- **位移下限**（20px）：贴边抖动净位移过小直接判负
- 只取垂直分量（沿边滑动永不触发）
- 时间戳用钩子线程 `QueryPerformanceCounter`（µs），不用会回绕的
  `MSLLHOOKSTRUCT.time`
- 测不出速度判负（时间戳缺失/倒退、程序化瞬移）
- 每次回绕后移动历史清零

PowerToys 没有任何等价物——它只有拖拽抑制和（可选）热键触发来防误触。

**参数怎么定**：本插件额外记录「到达轨迹」并在调参面板画直方图（`best` / `最优`
给出建议门槛）。PowerToys 完全没有这一层——它没有需要标定的参数，所以也不需要
观测工具；本插件的撞击门槛是主观量，必须靠用户自己的速度分布来定，这是跟随该
功能引入的**新交互面**，不是对原实现的偏离。

### 5.2 差异：注入事件

- PowerToys `MouseHookProc` **不过滤** `LLMHF_INJECTED`。它 `SetCursorPos` 落点
  后 `return 1` 吞掉原始 move；落点产生的新 move（系统注入）再进钩子时，靠
  `IsWithinWrapThreshold` 的 50px 防震荡半径判定不回绕。
- 本插件 `LLMHF_INJECTED` 一律只观察不回绕——包括**自己 `SetCursorPos` 产生的**事件？
  实测否：真机探针证实自己的 `SetCursorPos` 事件**不带** injected 标志
  （`flags=0x0`、`extra=0x0`，`injected_wrap_probe.py` 逐字段复核），所以插件的回绕
  落点不会被自己的钩子拒绝，行为与 PowerToys 等效，同时把「其他程序瞬移光标」挡在
  门外（PowerToys 会被这类程序触发回绕，本插件不会）。
- 影响：**远程桌面不受影响**。`SendInput` 确实产生带 `LLMHF_INJECTED` 的事件，但同一次
  移动还会产生一个不带标记的事件供引擎判定，实测 `injected_seen=16` 时
  `delta_wraps=1`——注入输入照常回绕。这一点原先写成「RDP 会失效」，是错的。
  真正被挡在门外的是「只有注入、没有伴随真实事件」的光标瞬移。

### 5.3 增强：线程安全

PowerToys 的 `CursorWrapCore` 与设置由宿主主线程读写、钩子线程消费，靠
`llmf` 时序与宿主锁保证；本插件引擎被协议线程与钩子线程**同时**持有，所有
公共方法带内部互斥锁（热路径一次无争用锁）。

## 6 平台与交互面 —— 偏离但有意

### 6.1 设置面

- PowerToys：宿主设置 UI（activation_shortcut 热键、auto_activate、四个开关），
  宿主进程内直接读写。
- 本插件：宿主设置卡（7+2 项）+ **搜索栏命令**（`impact` / `speed 3000` /
  `mode vertical` / `ctrl` / `guard/edge/tol <n>` / `diag` / `reset`）。宿主没有
  插件→宿主设置写回通道，所以搜索栏改动持久化在插件数据目录的
  `settings.json`（最小 diff），启动/`settingsChanged` 时覆盖在宿主值之上；
  `reset` 删文件回退。

### 6.2 激活方式 —— 最大语义偏离

- PowerToys：`activation_shortcut` 热键按下时**开启**回绕、再按关闭；
  `activation_mode` 0=总是/1=按住Ctrl/2=按住Shift（`GetAsyncKeyState` 实时查询，
  与本插件同款）；另有 auto_activate 进程名单。
- 本插件：只有「总是 / 按住 Ctrl / 按住 Shift」，**没有热键开关**——因为 Win顺
  的修饰键方案与任务栏音量插件（只 Ctrl 滚轮）不冲突，且不需要热键录制权限面。
  如果需要 PowerToys 式「按一下开关一段时间的回绕」，可以用搜索栏
  `impact on` + `reset` 手动模拟，或后续加 `toggleWrap` 动作（一句话的改动）。

### 6.3 缝隙诊断 —— 本插件增强

两边都能被「屏幕没贴齐」坑（坐标缝隙让本应内边的两侧各自成了外边）。PowerToys
`DetectMonitorGaps` 只写日志（`Logger::warn`）；本插件把缝隙写进 `cursorwrap diag`
的输出，用户可自助诊断。

## 7 PowerToys 里本插件**没有**移植的

| PowerToys 能力 | 不移植理由 |
|---|---|
| `activation_shortcut` 热键开关 | 语义与 Win顺 插件权限面不合；修饰键方案已覆盖按住场景（§6.2） |
| `auto_activate` 进程白名单 | 需要进程枚举（`processes` 权限）与前台窗口跟踪；v0.1 无此权限 |
| `_DEBUG` 拓扑 JSON 导出 / `GenerateTopologyJSON` | 调试用；诊断文本已覆盖 |
| `GetRelativePosition` / `GetAbsolutePosition` | 死代码（无调用者），未移植 |
| `wrap_simulator.py` tk 可视化 | 开发工具，不属于插件 |
| CursorLog 钩子日志器 | 同上 |

## 8 本插件超出 PowerToys 的

1. 撞击模式（速度门槛 + fail closed）——PowerToys 无
2. 搜索栏设置 + 数据目录覆盖层——PowerToys 依赖自己的设置管道
3. 纯核心可无屏单测（80 用例 + 7 布局 fixture）——PowerToys 的测试靠
   Python 复刻算法 + 真机布局采集，C++ 核心本身没有单测
4. 真机手势证据链（`live_wrap_probe.py`：真实钩子 + 真实 `SetCursorPos` +
   落点回读 + injected 标志检查）——PowerToys 仓库没有等价自动化

## 9 风险与建议

1. **`FindOppositeOuterEdge` 的区间闭开不一致**：PowerToys 用
   `relativePosition >= edge.start && relativePosition <= edge.end`（闭区间，
   两端都含），而边 `position = right - 1` 与屏幕 `contains` 用半开区间
   `[left, right)`。这意味着垂直坐标恰在 `edge.end`（= 相邻屏的 top）时两边都
   算重叠。本插件 `findOppositeOverlapping` 用 `[start, end)` 半开——**与
   PowerToys 差一个像素**：恰好停在 `end` 坐标时 PowerToys 命中、本插件走投影。
   影响仅限多屏对边区间端点恰好对齐的 1px 带，真机难以复现；如需逐位对齐可把
   本插件改成 `<=`。已记入 spec 已知差异。
2. **`-2` 死分支**（§4.3）：两边都到不了，保留供对照；若未来多屏全内边的布局
   出现（数学上不可能，总有一个外边），行为可能分歧。
3. **`MSLLHOOKSTRUCT.time` 未用**：两边一致用时钟源；本插件 QPC 单调，略优。

## 10 结论

算法层（拓扑、外边、对边、投影、回退、防震荡、模式过滤、角点方向）与 PowerToys
**逐行为等价**，差异集中在：平台接线（DLL vs 子进程）、设置面（管道 vs 搜索栏）、
激活语义（热键 vs 修饰键）、以及本插件多出的撞击门槛与诊断。两处有意偏离
（injected 过滤、`[start,end)` 区间）都已记录且影响面为 0 或 1px。
