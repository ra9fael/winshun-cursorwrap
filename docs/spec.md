# 光标回绕（`ra9fael.cursorwrap`）功能规格

状态：v0.1.0 已实现。协议与清单字段以 Win顺 0.22.0 插件文档为准（resident JSON-RPC over stdio）。
测试证据与未验证项见 [tests.md](tests.md)。English: [spec.en.md](spec.en.md)
实现与验证记录见 [validation.md](validation.md)。与 PowerToys 原版的逐行为对比见
[comparison.md](comparison.md)。

## 1 目标

- 多显示器下把光标从左/右/上/下外边「绕」到对边，省去长距离拖动
- 纯逻辑核心可脱离屏幕单测（显示器以结构体注入）
- 键盘路径：`cursorwrap` 关键字查看状态、开关、切模式、取诊断

## 2 非目标

- 不做托盘图标、托盘子菜单、设置窗口（v0.1 无任何窗口）
- 不做全局热键（激活方式是「按住 Ctrl / Shift」，不是热键注册）
- 不做光标「吸附」（snap / magnet），只做外边回绕
- 不复刻 PowerToys 的 `wrap_simulator.py` tk 可视化

## 3 命令（`cursorwrap` / `wrap` / `环绕`）

### 留空：状态行

- 标题：`光标回绕：已开启` / `光标回绕：已关闭`
- 副标题：`模式 <模式> · 激活 <方式> · 撞击开/关 [门槛] · N 块屏`；搜索栏改过设置时
  追加 `· 搜索栏覆盖中（reset 恢复宿主设置）`；钩子安装失败时追加
  `· 钩子未安装，仅显示状态`
- 动作（顺序即 Enter / Ctrl+Enter / Shift+Enter，宿主每行键盘最多三个）：
  - `toggleEnabled` 开启 / 关闭回绕
  - `cycleMode` 双向 → 仅上下 → 仅左右 → 双向
  - `toggleImpact` 开 / 关撞击模式

### 带文字：搜索栏直接改设置

`cursorwrap <词>`（大小写不敏感，中英文都认，整段文字是一条命令）：

| 输入 | 效果 |
|---|---|
| `impact` / `撞击` | 撞击模式开 ↔ 关 |
| `impact on` / `impact off` / `撞击 开` / `撞击 关` | 直接设 |
| `speed 3000` / `速度 3000` | 撞击门槛（500–20000，越界夹入；设速度即开撞击） |
| `mode` / `模式` | 方向循环：双向 → 仅上下 → 仅左右 |
| `mode vertical` / `模式 上下` 等 | 直接设方向 |
| `ctrl` / `shift` / `always` / `按住` / `总是` | 激活方式 |
| `guard 120` / `防震 120` | 防震荡半径（10–200） |
| `edge 5` / `边缘 5` | 边缘判定距离（1–20） |
| `tol 80` / `容差 80` | 邻接容差（10–200） |
| `reset` / `恢复` | 清掉搜索栏覆盖，宿主设置卡重新生效 |
| `diag` / `诊断` | 返回诊断行，Enter 复制全文 |
| 其他 | `valid:false` 的提示行，列出可用词 |

**持久化与优先级**：宿主没有插件→宿主的设置写回通道（协议只认
`winshun/log|notify|progress|refresh|hotkeys|tool`，其他方法静默丢弃），`WinShun.ini`
是宿主的文件，插件不碰。搜索栏的改动写进插件自己的数据目录
`WS_DATA_DIR\settings.json`（只存与默认值不同的键），启动、`settingsChanged` 时在
宿主设置之上覆盖；`reset` 删掉该文件。宿主设置卡对**没被覆盖的键**仍然权威——
两边都能改，状态行会注明当前来源。

## 4 手势（本插件唯一的自动化行为）

- 监听 `WH_MOUSE_LL` 的 `WM_MOUSEMOVE`，按当前设置判定是否回绕
- 命中时 `SetCursorPos` 到目的地并吞掉这一条移动消息（`return 1`）；失败的
  `SetCursorPos` 不吞事件
- 只处理 mousemove：按键、滚轮、`LLMHF_INJECTED`（程序注入）一律只看不绕，
  从不吞
- 判定顺序（与 PowerToys `HandleMouseMove` 一致）：单屏禁用 → 拖拽抑制 →
  激活键未按 → 防震荡半径 → 边缘判定 → 目的地计算

## 5 拓扑算法

移植自 PowerToys `MonitorTopology`（MIT，Microsoft）：

1. 每块屏 4 条边，用「拓扑内索引 + 边类型」作键（HMONITOR 会变，索引在单次快照内稳定）
2. 相邻判定：相反边类型、位置差 ≤ 邻接容差（默认 50px）、垂直区间重叠 > 容差
3. 与任何屏相邻的边是「内边」，永不回绕；其余为「外边」
4. 外边回绕目的地：先找坐标落在其区间内的对向外边（重叠区，保留垂直/水平坐标）；
   找不到则按坐标距离取最近的对向外边并把坐标夹进其区间（非重叠区投影，±1px 内缩）
5. 一条对向外边都找不到（单屏）时同屏回绕到自己的对边

显示器快照按「主屏优先，再按 left/top」排序，保证同一布局的索引稳定、测试可复现。

## 6 防震荡与方向

- 防震荡半径（`wrapGuardPx`，默认 50）：回绕落点周围该半径内不再触发；离开该范围后
  重新武装。没有它，落在对边上的光标会立刻原路弹回。
- 角点同时满足两条边时按移动主轴择优（水平移动优先左右边、垂直移动优先上下边），
  主轴候选失败再试另一条。

## 7 设置

| 键 | 类型 | 默认 | 说明 |
|---|---|---|---|
| `wrapMode` | choice | both | both / vertical（仅上下）/ horizontal（仅左右） |
| `activationMode` | choice | always | always / ctrl / shift（按住该键才回绕） |
| `disableDuringDrag` | bool | true | 按住左键时不回绕 |
| `disableOnSingleMonitor` | bool | false | 只有一块屏时是否禁用（PowerToys 默认不禁用） |
| `edgeThresholdPx` | number | 1 | 1–20，离外边多近算到达 |
| `wrapGuardPx` | number | 50 | 10–200，防震荡半径 |
| `adjacencyTolerancePx` | number | 50 | 10–200，内边判定容差 |
| `impactGate` | bool | false | 撞击模式：光标必须以足够速度撞到边缘才回绕 |
| `impactSpeedPps` | number | 2000 | 500–20000，撞击速度门槛（px/s） |
| `impactBandPx` | number | 150 | 50–500，撞击测速区间（距边缘多少 px 内才算逼近） |

非法值（类型不对、越界、NaN/Inf）一律回落到清单默认值，绝不报错、绝不产生越界行为。
`settingsChanged` 按部分更新合并：未给出的字段保留当前值。

### 撞击模式（impactGate）

关闭时（默认）：到达外边即回绕，与 PowerToys 原版一致。

开启后，回绕多一道门槛：**最后一段朝向边缘的连续逼近、在测速区间内的速度**
必须 ≥ `impactSpeedPps`。测速从最新样本向回追溯，遇下列任一情况即截断：

- 样本距边缘超过 `impactBandPx`（测速区间）——远处冲刺不计入；
- 逼近方向出现折返——抖动回弹后只量最后一段撞击；
- 两次事件间隔超过 100ms 窗口——停顿后从停顿之后重新起算；
- 时间戳缺失或倒退——测不出就判负（fail closed）。

设计取舍：

- 用「区间内最后一段」而不是全窗口平均——远处的高速冲刺会稀释近处的减速，
  让「远处飞过来、贴边慢慢挪」误判成撞击；截断到测速区间后只看贴边这一段。
- 位移下限（20px）：贴边 2px 内的抖动瞬时速度可达数千 px/s，但位移太小不是
  撞击——最后一段净位移 < 20px 直接判负。这与 findmouse v0.2 的
  `shakeMinSpeed` 结论一致：1000Hz 报点率下相邻事件的位移差是纯噪声。
- 只看**垂直分量**：沿边缘平行滑动（比如滑过去点任务栏图标）速度再快也不触发。
- **测不出速度就判负**（fail closed）：这挡住程序化瞬移（`SetCursorPos` 大跳）
  伪装成撞击。
- 时间戳用钩子线程的 `QueryPerformanceCounter`（微秒），不用 `MSLLHOOKSTRUCT.time`
  （毫秒精度且会回绕）。
- 回绕发生后移动历史清零：撞出去那一把的速度不会泄漏到下一次判定。

## 8 架构

单进程单文件 `cursorwrap.exe`，无 DLL、无 Qt、无窗口、无托盘。三个线程：

| 线程 | 职责 |
|---|---|
| 协议线程（SDK `serve()`） | `query` / `action` / `settingsChanged` / `shutdown`；持有一个 `WrapEngine` |
| 钩子线程 | `WH_MOUSE_LL` + 自己的消息泵；配置经 `WM_APP_SETTINGS` 投递，热路径无锁 |
| 监听线程 | message-only 窗口收 `WM_DISPLAYCHANGE` / `WM_SETTINGCHANGE` / `WM_DEVICECHANGE`，500ms 防抖后重建拓扑 |

纯逻辑（`wrap_topology`、`wrap_logic`）不引任何 OS 头文件，显示器以 `MonitorBox`
结构体注入，因此单测在任意平台可跑。

## 9 数据位置

不使用 `WS_DATA_DIR`：无状态落盘、无日志文件、无缓存。设置由宿主在 `WinShun.ini`
管理。诊断只在 `copyDiagnostics` 时进入剪贴板，且只含计数与几何信息（不含窗口标题、
文件名、用户文字）。

## 10 失败处理

| 失败 | 表现 | 降级 |
|---|---|---|
| `SetWindowsHookEx` 失败 | 状态行标「钩子未安装」，`winshun/log` 记 warn | 关键字查询/开关仍可用 |
| `SetCursorPos` 失败 | 不吞事件，原样放行 | 该次不移动，不产生「卡住」 |
| 显示器枚举为空 | 引擎空转，不吞任何事件 | 状态行显示 0 块屏 |
| 布局有坐标缝隙（GPU 驱动怪癖） | 缝隙 ≤ 容差仍算相邻；更大的缝隙按外边处理 | 诊断里列出缝隙 |
| 远程会话（RDP） | 只有一块虚拟屏 | 单屏语义（默认回绕到本屏对边） |
| 屏被热拔/热插 | 500ms 防抖后重建，移动历史清零 | 无崩溃、无错位 |
| 锁屏 / 安全桌面 | 收不到钩子事件 | 什么都不做（正确行为） |
| 设置值非法 | 回默认 | 状态行显示生效值 |
| 宿主太旧（无 `window`/`appearance`） | 忽略 | 关键字路径可用 |
| 连续 3 次查询超时 | 宿主重启插件 | `initialized()` 里重建钩子与拓扑 |

## 11 权限、隐私

- `permissions: ["ui"]`：低级鼠标钩子与（message-only）窗口。`ui` 在宿主侧只是
  部分强制，README 要写清楚。
- 不要求 `elevated`、`processes`、`network`、`files:write`、剪贴板权限。
- 坐标、显示器几何全部留在进程内，不出网、不落盘。

## 12 已知限制

- 需要低级鼠标钩子：若系统把钩子超时移除（`LowLevelHooksTimeout`），插件不做重建
  （与 findmouse 一致，见 tests.md 未验证项）。
- 独占全屏（exclusive fullscreen）D3D 应用里外框可能不生效。
- 高对比度/放大镜等辅助功能未专门验证。
- 与 PowerToys CursorWrap 的差异：不实现 `activation_shortcut` 热键触发模式，
  也不实现「自动激活」的进程白名单。

## 13 里程碑

- v0.1（本版）：上述全部
- v0.2 候选：按显示器单独开关、缝隙自动吸附建议、钩子超时后重建

## 14 实现记录（v0.1.0 回填）

### 已实测

- 单元测试 `cw_tests`：80 个用例 + 7 个布局 fixture 文件全绿（`ctest` 1/1 通过）。
- 协议往返 `tests/protocol_roundtrip.py`：45 项检查全通过（initialize → query →
  action ×4 → settingsChanged ×2 → cancel → shutdown），进程退出码 0。
- 构建：MinGW-w64 GCC 13.2 / CMake 3.29 / Ninja Release，`cursorwrap.exe` 静态链接
  C++ 运行时，产物落在仓库根（与 `plugin.json` 同名目录）。

### 与规格的偏差

- §5 的「同屏回绕 ±2px 内缩」实际不生效：因为单屏的左右边、上下边都是外边，
  `findOppositeOverlapping` 总能直接命中自己的对边，所以目的地是对边的
  `position`（`right-1` / `bottom-1`），而不是 `right-2`。PowerToys 的
  `GetWrapDestination` 同样先走「找到对边」分支，行为一致；回弹由防震荡半径吸收。
- §5 的「回绕到本屏对边」在双屏以上实际是「回绕到整体布局最外的对边」：
  例如两块横排屏，右侧屏的右外边回绕到左侧屏的左外边（位置最小的对向外边），
  这与 PowerToys 的 `FindNearestOppositeEdge` 语义一致（拖动光标跨越整个桌面）。
- `adjacencyTolerancePx` 通过 `Topology::initialize` 的容差参数生效；
  `edgeThresholdPx` 现在也真正接入判定（`Topology::onOuterEdge` 的
  `edgeThreshold` 参数），默认 1px 与 PowerToys 一致，调大后边缘判定带变宽。

### 未实测（发布阻塞项）

- 真实双屏/三屏下的手感与误判率：本轮只有单显示器环境，所有多屏断言都来自
  注入的几何快照。
- 真实鼠标移动下的 `SetCursorPos` 落点、拖拽抑制手感、按住 Ctrl 的互不干扰。
- 热插拔/`WM_DISPLAYCHANGE` 的真实重建时序（代码路径只有合成测试）。
- 远程会话、独占全屏、高对比度、放大镜。
- 在真实 Win顺 宿主里安装 `.wsplugin` 并启用（L3 目前是直接驱动 exe 的 stdio 往返，
  不是宿主集成）。
