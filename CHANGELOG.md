# Changelog

[English](CHANGELOG.en.md)

All notable changes to this plugin. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and versions follow
[Semantic Versioning](https://semver.org/). The version in `plugin.json` is the
single source of truth; release tags are `v<version>`.

## [Unreleased]

## [0.2.1] - 2026-11-13

### Added

- **远程桌面（RDP）兼容**：状态行与诊断信息标注 `远程桌面` / `remote session`，
  说明「这台机器平时双屏、现在只有一块」是会话类型而非故障；诊断新增
  `remote session` 与 `rebuilds refused (empty enumeration)` 两个字段。
- 单屏且 `disableOnSingleMonitor` 开启时，状态行明确写出
  `单屏禁用已生效：只有一块屏，回绕不会发生`——这是 RDP 下唯一会静默关掉回绕的
  设置项，不写出来用户会以为插件坏了。
- 新增 `tests/rdp_wrap_probe.py`、`tests/repeat_push_probe.py`、
  `tests/injected_wrap_probe.py`：用插件自己的计数（`wraps` / `moves` /
  分原因跳过）判断是否回绕，并校验落点是否为文档规定的内缩 1px。

### Fixed

- **RDP 重连/缩放会清空拓扑**：`EnumDisplayMonitors` 在会话重连或分辨率切换的瞬间
  可能返回**空列表**，而 `WM_DISPLAYCHANGE` 正好在那个窗口触发；旧代码会把空列表
  当成新布局，清掉可用拓扑并让插件在整段会话里失效。现在：
  1. `WrapEngine::rebuild` 拒绝空枚举（保留上一份可用布局）并计入
     `rebuildRefused`；
  2. `enumerateMonitors` 在枚举为空时回退到虚拟屏矩形
     （`SM_*VIRTUALSCREEN`），让通知永远不会把插件变成无布局状态。
  真正的布局变化（拔掉外接屏、会话结束回到双屏）仍然立即生效。
- **旧的单屏真机探针判定不成立**：`tests/live_wrap_probe.py` 用
  `cursor_pos() != target` 判定"回绕了"，但没走到目标是同一结果。实测证据里
  `after=[1888,540]`（目标 1919）被判成"回绕成功"，而单屏右边缘回绕的落点必须是
  `x=1`。新探针改为核对插件自己的 `wraps` 增量 + 落点坐标，`live_wrap_probe.py`
  的这份证据随之作废（见 `docs/validation.md`）。

### Verified on a real Remote Desktop session

`tests/repeat_push_probe.py` 在一台真实 RDP 会话（`remote=1`、1920×1080 单虚拟屏）
上各跑 3 轮：**6/6 全部回绕**，右边缘落点 `x=1`、左边缘落点 `x=1918`，
`y_stable=True`（证明没有外部力量干扰）。`tests/injected_wrap_probe.py` 另外证明
注入输入（`SendInput`，`LLMHF_INJECTED`）同样能回绕：`injected_seen=16` 且
`delta_wraps=1`——所以插件丢弃注入事件的过滤**不会**让 RDP 用户的真实输入失效。

## [0.2.0] - 2026-11-12

### Added

- **数据窗口**（`cursorwrap data` / `数据` / `window` / `窗口`，设置页"数据窗口"）：
  一个真实的置顶 GDI 窗口，不抢焦点（`WS_EX_NOACTIVATE`），150ms 自刷新，关掉即停。
  内容：状态头（开关 / 钩子 / 屏数 / 外边缘数）、引擎最后看到的光标位置与最近一次
  决策、到达速度直方图（对数横轴 500–20000，画出当前门槛与建议门槛）、按原因分类的
  跳过计数、最近 20 条决策日志（边、测得速度、裁决）。**这就是"不只离线分析"**：
  直方图和决策日志可以边调边看。
- **按原因分类的跳过计数**（`SkipReason`）：`no-monitors` / `single-monitor` /
  `dragging` / `no-modifier` / `guard` / `off-monitor` / `not-on-edge` /
  `impact-gate`。此前只有一个 `suppressed` 总数，无法回答"到底哪道门槛在拒"；
  现在诊断信息、`data` 子菜单和数据窗口都按原因列出。
- **决策日志**（`Documented Decision`，上限 24 条）：记录每次到达与每次"跳过原因
  变化"（同一次连续滑行只记一条，1000Hz 鼠标不会刷屏），含位置、屏、边、测得速度、
  当时门槛、裁决。
- **钩子健康看门狗**：独立线程每秒比对 `GetCursorPos()` 与钩子最后报告的位置；
  连续两个周期"指针动了但没事件"判定 Windows 已静默卸载钩子 → 卸下重装、计数、
  `log(warn)`。我们自己 `SetCursorPos` 后会立刻把落点写进"最后位置"，否则自己的
  回绕会被误判成钩子死亡（这是最关键的陷阱）。
- 设置页新增 `tuningOverlay`（调参可视化）与 `dataWindow`（数据窗口）两项，与搜索栏
  的 `tune` / `data` 是**同一个开关**——此前的调参开关只在搜索栏可见，设置页没有。
- 诊断信息补齐：分原因跳过计数、钩子事件 / 注入事件 / 重装次数 / 响应状态、
  `SetCursorPos` 失败次数、两个窗口的开关状态。

### Fixed

- **真机双屏完全无法回绕（根因一）**：钩子的回绕回调里直接调用 `refresh()`，
  即向宿主 stdout 写 JSON 并抢发送锁。低级鼠标钩子由 Windows 侧看门狗计时，
  回调超过 `LowLevelHooksTimeout`（默认 300ms）就会被**静默卸载**，而插件仍认为
  已安装、一切正常。回绕回调现在**只置一个原子标志**，由协议线程的 100ms 合并
  定时器统一 `refresh()`：钩子路径零 I/O。
- **真机双屏完全无法回绕（根因二）**：回绕路径用精确包含判断定位屏幕
  （`Topology::monitorAt`），指针一旦落在所有屏幕矩形之外就返回 -1，整条路径
  被当作 `off-monitor` 吞掉。真机运行记录到 **1008 次** 这类样本。新增
  `Topology::nearestMonitor`（包含则本身，否则最近），回绕路径改用它；
  `monitorAt` 保持精确语义（布局测试与 gap 检测依赖它）。新增回归用例：
  「指针在布局外 1px 不得关闭回绕」「布局外样本归到最近屏」。
- 回绕回调改走 `HC_ACTION` 显式判断，不再依赖 `code >= 0` 的隐式约定。
- 钩子句柄改为 `std::atomic<long long>` 持有，重装与退出都走同一处，
  不会重复安装泄漏或卸载错句柄。

### Changed

- 调参可视化与数据窗口从"命令层私有状态"改为 `cw::Settings` 字段
  （`tuningOverlay` / `dataWindow`），走与其它设置相同的夹取与持久化路径：
  设置页、搜索栏、`settings.json` 覆盖文件三处同源，重启后仍然记住。
- 数据窗口点 X 关闭时会同步关掉设置项，两个界面不会各说各话。
- 版本升到 `0.2.0`（新增功能面：minor）。

### Added

- **到达轨迹与最优门槛建议**（`cursorwrap best` / `最优`、`trace` / `轨迹`、
  `clear` / `清除轨迹`）：每次到达外边记一行（时间、屏、边、测得速度、当时门槛、
  是否回绕、门槛是否关闭），上限 512 行。**被门槛拦下的到达同样记录**，否则慢速簇
  永远缺席、直方图只有一半、建议无从算起；一次到达只记一行（手停在边缘被 Windows
  钳位时会产生成百上千个同坐标样本）。`suggestGate` 在去掉两端各 10% 异常值后取
  相邻速度间的最大相对空档，门槛落在空档中点；空档不足 25% 视为单簇，返回 0 并在
  界面明说"数据不足"，不编造数字。`trace` 复制 CSV（`seq,t_us,monitor,edge,
  measured_pps,gate_pps,wrapped,gate_off`）供离线核对。
- 调参面板里的**记录曲线直方图**（对数横轴 500–20000 px/s）：青色竖线为当前门槛，
  绿色竖线为建议门槛，标题显示 `N hits · M wrap`。固定在主屏工作区右下角，
  按 `rcWork` 定位，不会被任务栏切掉。
- 搜索栏补齐清单里的全部 10 个设置：新增 `drag` / `拖拽`、`single` / `单屏` 子菜单
  与 `drag on|off`、`single on|off` 直接设值；`tune` 改为子菜单（开关 + 区间 /
  门槛 / 防震 / 阈值四个下钻），`tune on|off` 直接开关。协议测试逐项核对
  「清单 10 个设置都有对应行」，防止两处漂移。
- **调参可视化**（`cursorwrap tune` / `调参`，或设置列表里的"调参可视化"行）：
  **只画外边缘，屏幕中间完全不动**——每条能回绕的外边一条 2px 主题色实线，线内侧
  `impactBandPx` 宽（最少 8px）的半透明带，内边缘（两屏相接）不画；撞到边缘时贴该边
  浮出 `实测 / 门槛 px/s` 面板（绿=回绕/黄=被拦/灰=门槛关闭）。逐像素 alpha
  （`UpdateLayeredWindow(ULW_ALPHA)`），每个边缘条只建一个 DIB（4K 的 150px 带
  约 1.5MB），静态条只在拓扑或区间宽度变化时重建。颜色跟随宿主主题
  `accent`/`text`/`background`；开关不持久化。

### Fixed

- **撞击模式在真机上"完全失效"**：手撞到边缘后 Windows 钳位指针，后续事件坐标
  重复；这些重复样本被计入平均速度，把真实的 2000 px/s 稀释成 1787 → … → 0，
  于是一个都过不了门槛。现在先剥掉钳位尾部，并且最后一步只量到最后一个**自由**
  样本，不再把"撞墙"当成减速。新增 4 个回归用例（含「12 个重复样本不能把
  2100 px/s 压到 2000 以下」）。
- **调参覆盖层变成整屏不透明遮罩**：色键透明只能全透明/全不透明，失败时整块屏
  被底色覆盖（用户截图里的青色面板）。改为逐像素 alpha + **只画外边缘**，
  中间区域不再有任何窗口。
- 调参面板被任务栏切掉一半：改为按 `rcArea`/`rcWork` 定位。
- **调参面板只画一次就冻结**：边缘条重建时销毁了持有重绘定时器的窗口，定时器随之
  消失。现在重建后在新条上重新 `SetTimer`；并把「门槛值变化」也纳入直方图的重绘
  条件（否则改门槛后竖线不移动）。真机验证：先注入 12 条慢速只有一簇，再注入 12 条
  快速后出现两簇、`best 2800` 竖线出现、标题从 `12 hits` 变 `24 hits · 12 wrap`。

### Changed

- **调参可视化重写**：从"每块屏整屏分层窗口 + 色键透明"改为"每条外边缘一个逐像素
  alpha 的窄条 + 一块读数面板 + 一块轨迹直方图"。色键只能全透明/全不透明，
  半透明带做不出来，失败时整屏被底色覆盖。
- **回绕落点内缩 1px**：从"对边边缘线"改为屏内 1px（`left+1` / `right-2` /
  `top+1` / `bottom-2`）。落点本身在判定带上会让防震圈被迫吞掉一次触发；
  内缩后落点干净地落在屏内，连穿更顺。
- 搜索栏多行设置列表从 11 行扩到 16 行，覆盖清单全部 10 个设置 + 调参 / 建议 /
  轨迹 / 诊断 / 恢复。
- `tune` / `调参` 从"直接切换开关"改为"打开调参子菜单"（`tune on|off` 仍直接开关），
  避免误触把覆盖层点亮。

### Changed (previous)

- **搜索栏改成多行设置列表**：`cursorwrap` 留空不再只返回一行状态，而是返回
  11 行设置列表——每行一个设置项，Enter 就是该行的主操作（开关 / 循环 / 下钻 /
  复制 / 恢复）。这样每个设置都有自己的键位，不再受宿主「每行最多三个键」的限制，
  也不用记 Ctrl+Enter / Shift+Enter 组合。
- **子菜单下钻**：`cursorwrap impact` / `speed` / `band` / `mode` / `activation` /
  `guard` / `edge` / `tol` 打开对应子菜单，**第一行永远是 `← 返回`**，其余每行一个
  可选值，Enter 直接应用。下钻靠行的 `setQuery` 字段（Enter 填入搜索框），
  为此给 vendored `winshun.h` 的 `Item` 补了 `setQuery` / `withSetQuery()`
  （宿主 `Items.cpp` 早已支持该字段，SDK 头没暴露）。
- 新增关键字 `activation` / `激活`（激活方式子菜单）；`band` / `区间`（测速区间）。
- 带数字仍然直接设值并显示对应子菜单（新值标「当前」）：`speed 3000`、
  `band 300`、`guard 120` 等。
- 未知词提示行更新为完整词表（含 band/区间、activation/激活）。

### Added

- 搜索栏直接改设置：`cursorwrap impact`（撞击开关）、`cursorwrap speed 3000`
  （撞击门槛）、`cursorwrap mode vertical`（方向）、`cursorwrap ctrl/shift/always`
  （激活方式）、`cursorwrap guard/edge/tol <n>`（阈值）、`cursorwrap reset`
  （恢复宿主设置卡）。改动立即生效并持久化到插件数据目录
  （`WS_DATA_DIR\settings.json`，只存与默认不同的键）；宿主设置卡对未覆盖的键
  仍然权威，状态行注明来源。中英文关键词、数字越界夹入、未知词给提示行。
- 新增动作 `toggleImpact`（Shift+Enter 切换撞击模式）。宿主每行键盘最多绑三个
  动作（Enter/Ctrl+Enter/Shift+Enter，第 4 个没有按键），复制诊断改走搜索栏：
  `cursorwrap diag` / `诊断` 返回诊断行，Enter 复制全文。
- 撞击模式（`impactGate` / `impactSpeedPps`）：开启后光标必须以足够速度撞到边缘才
  回绕，防止慢慢挪到边缘（比如去点任务栏）时误触发。速度按最后一段朝向边缘的连续
  逼近计算（500–20000 px/s，默认 2000）；沿边缘平行滑动不算；测不出速度
  （时间戳缺失/倒退、程序化瞬移）判负不回绕。真机验证：快推 ≈5300px/s 回绕、
  慢漂 ≈800px/s 与单次瞬移不回绕。
- 撞击测速区间（`impactBandPx`，50–500，默认 150）与去抖：测速只在距边缘该区间内
  进行——远处冲刺被截断，不会稀释贴边的减速；折返后只量最后一段撞击；贴边位移
  不足 20px 的抖动直接判负；停顿超过窗口后重新起算。移动历史加深到 128 条，
  覆盖 1000Hz 报点率下的完整窗口。
- 工程修复：`WrapEngine` 现在内部带互斥锁（此前被协议线程与钩子线程无锁共享）；
  钩子回调改用 `QueryPerformanceCounter` 时间戳（撞击模式测速用）。

## [0.1.0] - 2026-11-10

### Added

- 首个版本：多显示器光标回绕。光标到达屏幕外边时从对边进入，按显示边拓扑
  （`wrap_topology` 纯核心，移植自 PowerToys CursorWrap 的算法：边映射、外边判定、
  非重叠区投影），支持双向 / 仅纵向 / 仅横向三种模式、按住 Ctrl / Shift / 总是
  三种激活方式、拖拽抑制与单屏禁用。
- `cursorwrap`（`wrap` / `环绕`）关键字：留空列状态行，Enter 开关启用、
  Ctrl+Enter 切换回绕模式、Shift+Enter 复制诊断。
- `WM_DISPLAYCHANGE` / `WM_SETTINGCHANGE` 后 500ms 防抖重建显示器拓扑。
- 布局 fixture 回归测试（`tests/layouts/*.json`）与 3×3 网格场景单测。
