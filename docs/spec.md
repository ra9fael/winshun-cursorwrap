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

### 留空：设置列表（多行）

留空返回**多行设置列表**，每行一个设置项，Enter 就是该行的主操作——这样每个设置
都有自己的键位，不受宿主「每行最多三个键」的限制。**清单里的 12 个设置全部在这里
可达**（协议测试逐项核对，防止两处清单漂移）：

| 行 | 标题 | Enter | 对应清单设置 |
|---|---|---|---|
| 1 | `光标回绕：已开启/已关闭` | 开启 / 关闭回绕 | —（运行时开关） |
| 2 | `撞击模式：开/关` | 切换撞击模式 | `impactGate` |
| 3 | `撞击速度：<n> px/s` | 下钻到速度子菜单 | `impactSpeedPps` |
| 4 | `撞击测速区间：<n> px` | 下钻到区间子菜单 | `impactBandPx` |
| 5 | `回绕模式：<模式>` | 循环 双向 → 仅上下 → 仅左右 | `wrapMode` |
| 6 | `激活方式：<方式>` | 下钻到激活子菜单 | `activationMode` |
| 7 | `防震半径：<n> px` | 下钻到半径子菜单 | `wrapGuardPx` |
| 8 | `边缘阈值：<n> px` | 下钻到阈值子菜单 | `edgeThresholdPx` |
| 9 | `内边容差：<n> px` | 下钻到容差子菜单 | `adjacencyTolerancePx` |
| 10 | `拖拽时不回绕：开/关` | 下钻到开/关子菜单 | `disableDuringDrag` |
| 11 | `单屏时禁用：开/关` | 下钻到开/关子菜单 | `disableOnSingleMonitor` |
| 12 | `调参可视化：开/关` | 下钻到调参子菜单 | `tuningOverlay` |
| 13 | `数据窗口：开/关` | 下钻到数据窗口子菜单 | `dataWindow` |
| 14 | `建议门槛：<n>/数据不足` | 下钻到建议子菜单 | 由轨迹算出的 `impactSpeedPps` 建议 |
| 15 | `光标回绕：到达轨迹` | 复制轨迹 CSV | — |
| 16 | `光标回绕：诊断信息` | 复制诊断全文 | — |
| 17 | `恢复宿主设置卡` | 清除搜索栏覆盖 | — |

**设置页与搜索栏同源**：`tuningOverlay` 与 `dataWindow` 是清单里的真设置，
设置页的开关、搜索栏的 `tune` / `data` 子菜单、`settings.json` 覆盖文件写的是
同一个字段，重启后仍然记住。此前调参开关只在搜索栏可见，设置页没有。

状态行副标题：`模式 <模式> · 激活 <方式> · 撞击开/关 [门槛] · N 块屏`；搜索栏改过
设置时追加 `· 搜索栏覆盖中（reset 恢复宿主设置）`；钩子安装失败时追加
`· 钩子未安装，仅显示状态`。

### 子菜单（下钻）

下钻行带 `setQuery`：Enter 把 `cursorwrap <词> ` 填进搜索框，重新查询后显示子菜单。
**子菜单第一行永远是 `← 返回`**（`setQuery` 回 `cursorwrap `），其余每行一个可选值，
Enter 直接应用：

| 输入 | 子菜单 |
|---|---|
| `impact` / `撞击` | 返回 · 撞击开关 · 撞击速度 · 测速区间 |
| `speed` / `速度` | 返回 · 500/1000/2000/3000/5000/8000/12000/20000 px/s |
| `band` / `区间` | 返回 · 50/80/120/150/200/300/500 px |
| `mode` / `模式` | 返回 · 双向 · 仅上下 · 仅左右 |
| `activation` / `激活` | 返回 · 总是 · 按住 Ctrl · 按住 Shift |
| `guard` / `防震` | 返回 · 10/30/50/80/120/200 px |
| `edge` / `边缘` | 返回 · 1/2/5/10/20 px |
| `tol` / `容差` | 返回 · 10/30/50/80/120/200 px |
| `drag` / `拖拽` | 返回 · 开 · 关（拖拽时不回绕） |
| `single` / `单屏` | 返回 · 开 · 关（单屏时禁用） |
| `tune` / `调参` | 返回 · 开关 · 测速区间 · 门槛 · 防震 · 边缘阈值（见下文"调参可视化"） |
| `data` / `window` / `数据` / `窗口` | 返回 · 开关 · 各类跳过计数 · 钩子健康 · 复制轨迹 · 建议门槛（见下文"数据窗口"） |
| `best` / `最优` | 返回 · 建议门槛 · 采用 · 查看数据（见下文"记录曲线"） |

### 带数字：直接设值（跳过子菜单）

`cursorwrap <词> <数字>` 直接设值并显示对应子菜单（新值标「当前」）：

| 输入 | 效果 |
|---|---|
| `impact on` / `impact off` / `撞击 开` / `撞击 关` | 直接设撞击开关 |
| `speed 3000` / `速度 3000` | 撞击门槛（500–20000，越界夹入；设速度即开撞击） |
| `band 300` / `区间 300` | 测速区间（50–500） |
| `mode vertical` / `模式 上下` 等 | 直接设方向 |
| `ctrl` / `shift` / `always` / `按住` / `总是` | 激活方式 |
| `guard 120` / `防震 120` | 防震荡半径（10–200） |
| `edge 5` / `边缘 5` | 边缘判定距离（1–20） |
| `tol 80` / `容差 80` | 邻接容差（10–200） |
| `drag on` / `drag off` / `拖拽 开` / `拖拽 关` | 拖拽时是否回绕 |
| `single on` / `single off` / `单屏 开` | 单屏时是否禁用 |
| `tune on` / `tune off` / `调参 开` / `可视化 关` | 调参可视化开关（不带值则开子菜单） |
| `data on` / `data off` / `数据 开` / `数据 关` | 数据窗口开关（不带值则开子菜单） |
| `trace` / `轨迹` | 返回轨迹行，Enter 复制 CSV |
| `best` / `最优` | 建议门槛子菜单：查看 / 采用 / 看数据 |
| `clear` / `清除轨迹` | 清空已记录的到达轨迹 |
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
| `tuningOverlay` | bool | false | 调参可视化：在能回绕的外边缘画测速区间与门槛线，并显示实测速度 |
| `dataWindow` | bool | false | 数据窗口：实时直方图、决策日志、按原因跳过计数、钩子健康 |

`tuningOverlay` 与 `dataWindow` 是**真设置**，不是命令层的临时状态：设置页、搜索栏
（`tune` / `data`）、`settings.json` 覆盖文件写的是同一个字段，重启后仍然记住。
两个窗口的打开/关闭由 `applyWindowSettings()` 统一执行，任何一条路径改完都会调用它。

回绕落点在**对边内缩 1px**（`left+1` / `right-2` / `top+1` / `bottom-2`），不在判定
带上——落点本身算"在边缘"会让 guard 被迫吞掉一次触发。

### 调参可视化（tuning overlay）

搜索栏 `tune` / `调参` 打开调参子菜单（开关 + 区间 / 门槛 / 防震 / 阈值四个下钻），
`tune on` / `tune off` 直接开关。开启后：

**只画外边缘，屏幕中间完全不动。** 这是两次返工得到的结论：第一版用色键透明
铺满整屏，色键失败时整块屏变成不透明底色（用户截图里的青色遮罩），而且中间
本来就不该有任何东西。现在的画法是：

| 可视化 | 画什么 |
|---|---|
| 外边缘线 | 每条**能回绕**的外边一条 2px 主题色实线（alpha 235） |
| 测速区间 | 该线内侧 `impactBandPx` 宽（最少 8px）的**半透明**带（alpha 24） |
| 内边缘 | **不画**：两屏相接处不会回绕，画了只是噪声 |
| 速度读数 | 最近一次到达边缘时，贴该边浮出小块面板：`实测 / 门槛 px/s` 加边框状态色（绿=回绕、琥珀=被拦、灰=撞击关闭） |
| 记录曲线 | 主屏工作区右下角一块直方图面板（见下节） |

实现要点：

- **必须逐像素 alpha**（半透明带 + 清晰线），所以用
  `UpdateLayeredWindow(ULW_ALPHA)`，不是 `LWA_COLORKEY`（色键只能全透明或全不透明）。
- **每个边缘条只建一次 DIB**：4K 屏上 150px 带约 1.5MB；早前按帧新建
  `2560×1440` 的 DIB（约 33MB/帧）直接撞穿插件 64MB 的 job 上限并崩溃。
- 静态条只在「拓扑变化 / 区间宽度变化」时重建；读数面板只在判定或数值变化时重绘。
- 窗口样式 `WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_NOACTIVATE |
  WS_EX_TOOLWINDOW | WS_EX_TOPMOST`，`WM_NCHITTEST → HTTRANSPARENT` 点击穿透；
  面板按 `rcWork` 定位，不会被任务栏切掉一半。
- 颜色跟随宿主 `appearance` 的 `accent` / `text` / `background`。
  开关状态不持久化：宿主重启后覆盖层默认关闭。

### 记录曲线与「最优门槛」（arrival trace）

只给实时读数，用户永远找不到最优值——它一次只回答一个问题。真正要回答的是
**分布**问题：*故意甩*产生的速度簇 和 *顺路经过*产生的速度簇在哪里分开。

引擎因此记录**到达轨迹**（`Arrival`）：每次到达外边记一行
`t_us / monitor / edge / measured_pps / gate_pps / wrapped / gate_off`，
上限 512 行（`kTraceMax`）。三条关键设计：

1. **被门槛拦下的到达也记**。第一版只记通过的，结果是慢速簇永远缺席，直方图
   只有一半，「最优」无从算起。记录发生在门槛判定**之前**。
2. **一次到达只有一行**。手停在边缘被 Windows 钳位时会产生成百上千个同坐标
   样本，它们属于同一次到达（`m_hasArrivalEdge` 按 `edge+monitor` 去重）。
3. **一条记录能反推所有门槛**：只看 `measured_pps` 就能回答"若门槛为 X 会不会
   回绕"，所以 512 行足够扫遍 500…20000。

`best` / `最优` 给出建议值（`suggestGate`，纯函数）：

- 取所有可测速度排序，在去掉两端各 10% 异常值后的区间里找**相邻值的最大相对
  间隔**，门槛取该间隔中点；
- 相对间隔 < 25% 说明只有一个簇（没有空档）→ 返回 0，界面明说"数据不足"，
  **不编造数字**；
- 结果钳到 500–20000。

`trace` / `轨迹` 把 CSV 复制到剪贴板（`seq,t_us,monitor,edge,measured_pps,
gate_pps,wrapped,gate_off`），可以离线画图或验证建议值；`clear` / `清除轨迹` 清空。

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

## 8 数据窗口（data window）

调参可视化画在边缘上，但它跟着桌面走、也只在撞边那一刻有内容。**数据窗口**是
可以一直摆在旁边的窗口：

- 真实 GDI 顶层窗口（`WS_POPUP | WS_CAPTION | WS_SYSMENU`），
  `WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE`：置顶、不进任务栏和
  Alt+Tab、**绝不抢焦点**（否则调参时会打断正在操作的应用）。
- 150ms 自刷新（`SetTimer` + `InvalidateRect`），`WM_ERASEBKGND` 返回 1 后整屏
  自绘，不闪。
- 内容自上而下：
  1. 状态头：开关 / 钩子状态 / 屏数 / 外边缘数；
  2. 光标位置与**最近一次决策**（裁决 + 边 + 测得速度 + 当时门槛），
     以及 `moves / wraps / skipped` 累计；
  3. 到达速度直方图：对数横轴 500–20000 共 22 个桶，橙线为当前门槛、绿线为
     建议门槛，标题显示 `N hits` 与建议值；
  4. 按原因分类的跳过计数；
  5. 最近 20 条决策日志（边 · 测得速度 · 裁决 · 屏号），回绕绿、跳过灰。
- 点 X 关闭时把 `dataWindow` 设置同步关掉（插件线程每 100ms 轮询
  `takeCloseRequest()`），两个界面不会各说各话。

## 9 按原因分类的跳过计数与决策日志

只有一个 `suppressed` 总数时，"完全不能回绕"无法定位：九道门槛里任何一道都可能
一直说不。`SkipReason` 把它拆开：

| 原因 | 含义 |
|---|---|
| `no-monitors` | 一个显示器都没枚举到 |
| `single-monitor` | 只有一块屏且启用了"单屏时禁用" |
| `dragging` | 按住左键拖动，且启用了"拖拽时不回绕" |
| `no-modifier` | 激活方式要求按住 Ctrl/Shift，但没按 |
| `guard` | 落在防震荡半径内 |
| `off-monitor` | 指针不在任何显示器上（正常不该出现） |
| `not-on-edge` | 不在外边缘判定带内 |
| `impact-gate` | 到达了边缘，但测速没过门槛 |
| `wrapped`（`None`） | 回绕了 |

`Stats::skipped[]` 按原因累计**每一个样本**；`Decision` 日志只在
「到达」或「跳过原因变化」时记一条——一次连续滑行算一条，1000Hz 鼠标不会刷屏。
上限 24 条（`kDecisionMax`）。诊断信息、`data` 子菜单、数据窗口读的是同一份数据。

## 10 架构

单进程单文件 `cursorwrap.exe`，无 DLL、无 Qt、无托盘。四个线程：

| 线程 | 职责 |
|---|---|
| 协议线程（SDK `serve()`） | `query` / `action` / `settingsChanged` / `shutdown`；持有一个 `WrapEngine` |
| 钩子线程 | `WH_MOUSE_LL` + 自己的消息泵；配置经 `WM_APP_SETTINGS` 投递，热路径无锁 |
| 监听线程 | message-only 窗口收 `WM_DISPLAYCHANGE` / `WM_SETTINGCHANGE` / `WM_DEVICECHANGE`，500ms 防抖后重建拓扑 |
| 合并定时器线程 | 100ms 轮询：把钩子置的 `refreshPending` 合成一次 `refresh()`；把数据窗口的关闭请求同步成设置 |
| 看门狗线程（钩子内） | 1s 比对 `GetCursorPos()` 与钩子最后报告的位置，判定钩子是否被 Windows 静默卸载 |
| 窗口线程 | 调参覆盖层与数据窗口各自的 `GetMessage` 循环 |

### 钩子线程零 I/O（关键约束）

低级鼠标钩子由 Windows 侧看门狗计时：回调超过 `LowLevelHooksTimeout`（默认 300ms）
就被**静默卸载**，而插件仍认为 `installed == true`。因此钩子回调里**不做任何 I/O**，
连一行日志都不写：

- 回绕只置 `m_refreshPending` 原子标志，由合并定时器统一 `refresh()`；
- 看门狗只在原子量上比对位置，重装靠 `PostThreadMessage(WM_APP_REINSTALL)`。

**最容易踩的陷阱**：`SetCursorPos` 成功后必须立刻把落点写进「钩子最后看到的位置」，
否则下一次看门狗比对会看到「指针动了但没有事件」，把插件自己的回绕误判成钩子死亡。

### 指针定位用最近屏，不是精确包含

`Topology::monitorAt` 是精确包含判断（布局测试与 gap 检测依赖它，屏外返回 -1）。
回绕路径用的是 `Topology::nearestMonitor`：包含则本身，否则最近的一块屏。
真机上指针可能短时落在所有屏幕矩形之外（拓扑尚未重建、热插拔缝隙、Windows 报告的
显示器之间有空洞），此时精确判断会把整条路径判成 `off-monitor` 并吞掉回绕——
真机运行中记录到 1008 次。纯逻辑（`wrap_topology`、`wrap_logic`）不引任何 OS
头文件，显示器以 `MonitorBox` 结构体注入，因此单测在任意平台可跑。

### 远程桌面（RDP）

RDP 会话对本插件的影响有三点，且都不是"不支持"：

1. **只有一块虚拟屏**。这是正常配置：单屏时回绕到本屏对边（右边缘 → 左边缘
   `x=1`，左边缘 → 右边缘 `x=1918`）。真机 RDP 会话实测 6/6 回绕成功。
2. **`disableOnSingleMonitor` 会静默关掉回绕**。它是单屏下唯一会让用户以为
   "插件坏了"的设置项，所以状态行在命中该条件时明确写出
   `单屏禁用已生效：只有一块屏，回绕不会发生`，并标注 `远程桌面`。
3. **重连/缩放会触发 `WM_DISPLAYCHANGE`，而此时枚举可能为空**。这是真 bug：
   旧代码把空列表当成新布局，清掉拓扑后整段会话失效。现在 `rebuild` 拒绝空枚举
   （保留上一份布局并计入 `rebuildRefused`），`enumerateMonitors` 在为空时回退到
   `SM_*VIRTUALSCREEN` 矩形。真正的布局变化（拔屏、会话结束回到双屏）仍然立即生效。

注入输入不影响 RDP：`SendInput` 产生的事件带 `LLMHF_INJECTED`，插件丢弃这些事件的
过滤看似会挡住 RDP 的真实输入，实测证明不会——`injected_seen=16` 的同时
`delta_wraps=1`，因为同一移动还会产生一条不带该标志的事件供引擎判定。

### 微秒时钟必须防溢出（`qpc_time.h`）

钩子把 `QueryPerformanceCounter` 换算成微秒喂给引擎。写成
`counter * 1'000'000 / freq` 是错的：Windows 的 `freq` 是 10MHz，`counter * 1e6` 在
开机约 **10.7 天**（`2^63 / 1e6 / 1e7` 秒）后溢出 `int64`，之后每个时间戳都是负数。
后果不是"速度不准"，而是**门槛永远判负**：`approachSpeedPps` 用负窗口算出
`measured_pps = 0`，每一次到达都被 `impact-gate` 拒绝——表现就是「开了撞击模式，
怎么推都不回绕」。实测（开机 11.2 天）：`t_us = -875612248758`、`measured_pps = 0`。

`counterToUs(now, freq)` 把除法拆开：`whole = now / freq`、`rem = now % freq`，
`whole * 1e6 + rem * 1e6 / freq`，两个乘积都远小于 int64 上限。`freq <= 0` 或负计数
返回 0（fail closed）。**所有**取微秒的地方都必须用它，不允许再出现裸表达式。

## 9 数据位置

设置由宿主在 `WinShun.ini` 管理；搜索栏改动写进 `WS_DATA_DIR\settings.json`
（只存与默认值不同的键）。除此之外无日志文件、无缓存。诊断只在
`copyDiagnostics` 时进入剪贴板，且只含计数与几何信息（不含窗口标题、文件名、
用户文字）。

## 10 失败处理

| 失败 | 表现 | 降级 |
|---|---|---|
| `SetWindowsHookEx` 失败 | 状态行标「钩子未安装」，`winshun/log` 记 warn | 关键字查询/开关仍可用 |
| 钩子被 Windows 静默卸载 | 看门狗 2 个周期后重装并计数，`log(warn)` | 自动恢复，无需重启插件 |
| `SetCursorPos` 失败 | 不吞事件，原样放行，计入 `setcursorpos failures` | 该次不移动，不产生「卡住」 |
| 显示器枚举为空 | 引擎空转，不吞任何事件 | 状态行显示 0 块屏 |
| 指针短暂落在所有屏幕矩形之外 | 归到最近的一块屏继续判定 | 不再当作 `off-monitor` 吞掉 |
| 远程桌面（RDP）重连/缩放导致枚举为空 | `rebuild` 拒绝空枚举并计数，`enumerateMonitors` 回退到虚拟屏矩形 | 保留上一份可用布局，会话内不再失效 |
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

- 需要低级鼠标钩子。钩子被 Windows 超时移除时，看门狗（1s 比对位置）能发现并重装，
  但极端情况下（每次重装后立刻又被移出）会表现为「事件数不增长」——诊断里的
  `hook reinstalls` 与 `hook responsive` 就是给这种情况用的。
- 独占全屏（exclusive fullscreen）D3D 应用里外框可能不生效。
- 高对比度/放大镜等辅助功能未专门验证。
- 远程桌面（RDP）**是支持的**：会话里只有一块虚拟屏，走单屏语义（见 §10）。断开重连
  不再清空拓扑，状态行会标出 `远程桌面`。
- 与 PowerToys CursorWrap 的差异：不实现 `activation_shortcut` 热键触发模式，
  也不实现「自动激活」的进程白名单。

## 13 里程碑

- v0.1：多显示器回绕、撞击模式、测速区间与去抖、调参可视化、到达轨迹与建议门槛
- **v0.2（本版）**：数据窗口、按原因分类的跳过计数与决策日志、钩子健康看门狗、
  钩子线程零 I/O、指针按最近屏归位（真机双屏失效的两个根因）、两个窗口改为真设置
- v0.3 候选：按显示器单独开关、缝隙自动吸附建议、数据窗口里直接拖动门槛

## 14 实现记录（v0.1.0 回填）

### 已实测

- 单元测试 `cw_tests`：102 个用例 + 6 个布局 fixture 文件全绿（`ctest` 1/1 通过）。
- 协议往返 `tests/protocol_roundtrip.py`：103 项检查全通过（initialize → query →
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
- 独占全屏、高对比度、放大镜。
- 远程会话的断开重连时序（`rebuildRefused` 只有单测；**单屏 RDP 回绕本身已实测**
  6/6，见 `docs/validation.md`）。
- 在真实 Win顺 宿主里安装 `.wsplugin` 并启用（L3 目前是直接驱动 exe 的 stdio 往返，
  不是宿主集成）。

### v0.2.0：改了什么、还有什么没验证

用户在真机双屏上启用后反馈：**完全不能回绕**（开不开撞击模式都一样），
而且调参开关与数据读数在设置页里都找不到。审计定位出两个根因，都已修复并补了回归：

1. 钩子的回绕回调里直接调用 `refresh()`，即在 `WH_MOUSE_LL` 回调内向宿主 stdout
   写 JSON。回调超过 `LowLevelHooksTimeout`（默认 300ms）时 Windows 会**静默卸载**
   低级钩子。现在回调只置一个原子标志，由合并定时器每 100ms 合成一次 `refresh()`。
2. 回绕路径用精确包含判断定位屏幕，指针一旦落在所有屏幕矩形之外就被判成
   `off-monitor` 并吞掉。真机抓取中记录到 **1008 次** 这类样本。新增
   `Topology::nearestMonitor`，把这种指针归到最近的一块屏。

**仍未验证的**（必须如实说明）：

- **两个根因是靠读代码 + 一次真机抓取得出的，不是靠一次成功的真机回绕。**
  回归用例 `user_config_*`（`tests/test_impact_gate.cpp`）复现了用户的真机几何与
  设置并且全绿，但它们直接驱动引擎。真机手势是否已经恢复，只能由用户确认。
- 看门狗的重装路径从未在真机上触发过：单测覆盖了 `nearestMonitor` 与决策计数，
  看门狗只在数据窗口里被间接观察到（显示 `hook live`）。
- 数据窗口被渲染并检查过一次（截图里状态头、光标、直方图、跳过计数、决策日志都有
  真实数据），但混合 DPI 布局下的表现未验证。
