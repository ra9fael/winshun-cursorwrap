# 光标回绕（`ra9fael.cursorwrap`）测试规格

分层按工作台规范 `winshun-dev/DEVELOPMENT.md` §3（L0–L5）。每个测试项注明它验证
[spec.md](spec.md) 的哪一节。English: [tests.en.md](tests.en.md)

## L0 — 清单与打包静态校验

| 检查 | 对应 | 状态 |
|---|---|---|
| `plugin.json` 的 `$schema`、`manifest: 1`、`id == 目录名`、三段版本号、`author`、`license` | §7 | 通过（人工 + `tests/validate.py`） |
| `permissions` 只有 `ui`；无 `elevated` / `network` / `processes` | §11 | 通过 |
| `runtime`：resident / eager / `idleExitSec 0` / `normal` / `timeoutMs 1500` / `memoryMB 64` | §8 | 通过 |
| 一个命令、三个动作、顺序 `toggleEnabled, cycleMode, copyDiagnostics` | §3 | 通过（L3 也有断言） |
| 七个设置项类型在白名单内、默认值在文档区间内 | §7 | 通过 |
| 十二个设置项（含 `tuningOverlay` / `dataWindow`）类型在白名单内、默认值在文档区间内 | §7 | 通过 |
| 搜索栏覆盖清单每一项（协议测试从 `plugin.json` 逐键核对） | §3 | 通过（L3） |
| 打包物含 `plugin.json` / `cursorwrap.exe` / `README.md` / `LICENSE`，无 `build/`、`dist/`、`.git` | §8 | `tools/package.py` + `tools/ci_release.py` |
| 双语文档锚点互链（`docs/spec*.md`、`docs/tests*.md`、README、CHANGELOG） | — | 通过 |

## L1 — 纯逻辑单测（`cw_tests`，ctest 目标，155 用例 + 6 布局 fixture）

不引 OS 头文件，任何平台可跑。运行：

```powershell
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
# 或直接跑，带 fixture 目录：
.\build\cw_tests.exe .\tests\layouts
```

| 组 | 覆盖 | 对应 |
|---|---|---|
| 单屏拓扑 | 4 条边全为外边；左右/上下互绕 | §5 |
| 双屏横排 | 内边（相邻）不绕；外边绕到整体最外对边 | §5 |
| 双屏竖排 | 上下相邻边为内边；主屏排序后索引稳定 | §5 |
| L 形三屏 | 8 条外边；某屏上边为内边、另一屏上边为外边 | §5 |
| 非重叠投影 | 对角错位双屏：越出目标边区间时坐标夹进区间（±1px） | §5 |
| 模式过滤 | HorizontalOnly 忽略上下边、VerticalOnly 忽略左右边 | §4 |
| 角点方向优先 | 左上角向左移动选 Left、向上移动选 Top | §6 |
| 邻接容差 | 30px 缝隙在 50px 容差下算相邻、10px 容差下算外边 | §5 |
| 缝隙诊断 | 30px 缝隙被 `detectGaps` 报出；贴合布局无缝隙 | §10 |
| 空/退化拓扑 | 空列表、零面积屏不崩溃、不回绕 | §10 |
| 负坐标与极端坐标 | 主屏左侧屏（负坐标）正常回绕；±2^31 附近不溢出 | §10 |
| 混合 DPI | 144dpi + 96dpi 并排仍按物理像素判定内边 | §5 |
| 状态机：防震荡 | 落点半径内不重复触发、离开后重新武装 | §6 |
| 状态机：单屏禁用 | `disableOnSingleMonitor` 时不回绕 | §7 |
| 状态机：拖拽抑制 | 左键按下时不回绕，松开后恢复 | §7 |
| 状态机：激活键 | WhileCtrl / WhileShift 未按键时不回绕 | §7 |
| 状态机：模式 | VerticalOnly 下横向边忽略、纵向边回绕 | §4 |
| 设置解析 | 越界回默认、边界值接受、未知枚举保持原值、部分更新合并 | §7 |
| 设置类型混淆 | 空字符串枚举被忽略、NaN/±Inf 回默认 | §7 |
| 撞击模式 | 关闭时行为不变；慢速漂移不回绕；快速撞击回绕；只看垂直分量（沿边滑动不触发）；时间戳缺失/混合/程序化瞬移判负；回绕后历史清零；速度门槛边界值 500/20000 接受、499/20001 回默认 | §7 撞击模式 |
| 撞击测速区间与去抖 | 远处冲刺后贴边慢行不回绕（区间截断）；远处冲刺后区间内减速也不回绕；贴边 2px 抖动判负（位移下限）；折返后只量最后一段（快回弹判正、慢回判负）；停顿超过窗口重新起算；区间 20px 整位移恰好通过；左右/上下边缘对称；区间边界值 50/500 接受、49/501 回默认 | §7 撞击模式 |
| 调参判定快照 | 关门槛时 verdict 标 `gateOff`/`wrapped` 且带落点；门槛拦截时带测得速度与门槛值；快速通过时带落点坐标；guard 吸收不产生新 verdict；`resetState` 清空 verdict | §7 调参可视化 |
| 钳位样本不稀释测速 | 贴边连续同坐标样本（Windows 钳位）后，测得速度仍是那一段的自由速度；12 个重复样本不足以把 2100 px/s 压到 2000 px/s 以下；只有重复样本时 fail closed 不回绕 | §5 撞击模式 |
| 到达轨迹 | 每次到达外边记一行；被门槛拦下的也记；同一次到达（手停在边缘被钳位）只记一行；上限 `kTraceMax`；`clearArrivals` 清空；`resetState` 清空 | §7 记录曲线 |
| 建议门槛 `suggestGate` | 样本不足 6 条返回 0；两簇之间取最宽相对空档的中点；单一簇返回 0（不编造）；忽略不可测样本；结果钳在 500–20000 | §7 记录曲线 |
| 诊断结构 | 屏数/外边数/钩子标志/缝隙文本 | §3 |
| 按原因分类的跳过计数 | 每道门槛（拖拽 / 不在边上 / 撞击门槛）各自计数，且各原因之和等于 `suppressed` 总数 | §9 |
| 决策日志 | 一次连续滑行只记一条（不刷屏）；回绕记为 `wrapped`+`onEdge`；上限 `kDecisionMax` | §9 |
| `lastSample` | 被抑制的移动也算「引擎最后看到的位置」 | §8 |
| 最近屏归位 | `monitorAt` 保持精确（屏外 -1，布局测试依赖）；`nearestMonitor` 屏外归最近屏、空布局仍 -1；布局外 1px 的指针不得关闭回绕 | §5 |
| 用户真机配置复现 | 双屏 `[-2560,0..0,1440] + [0,0..2560,1440]`、`wrapMode=horizontal`、`guard=100`、`tol=10`：关门槛双向回绕落点正确；内边永不回绕；开门槛时 5000 px/s 命中回绕；慢漂只被 `impact-gate` 拦下且其他原因计数为 0；拖拽计入 `dragging` | §5 §9 |
| 远程桌面（RDP） | 单虚拟屏 1920×1080 双向回绕落点为 `x=1` / `x=1918`；`disableOnSingleMonitor` 计入 `single-monitor`；**空枚举不得清空拓扑**（保留上一份布局 + 计入 `rebuildRefused` + 之后仍能回绕）；真正的布局变化（拔屏、会话结束回双屏）仍然立即生效；单屏下撞击门槛仍可测速并正确拦截 | §5 §10 |
| 微秒时钟不溢出 | `counterToUs` 在开机 30 天、11.2 天真实计数值下都返回正数且数值正确；1ms 步长仍精确为 1000µs；`freq<=0` / 负计数 fail closed 返回 0；**端到端**：用「开机 11 天」的计数器喂引擎，6000 px/s 的撞击仍然回绕且 `measuredPps>=2000`（旧实现在这里 `measured_pps=0`，门槛拒绝一切） | §5 §9 |
| 撞击加速度门槛 | 匀速 sweep（`accel=0`）被拒绝；真加速冲边回绕；减速冲边（负值）拒绝；`accelMeasured` 区分「测出 0」与「测不出」；设置 0 保留为关闭、非零低于 200 抬到 200、超范围回退默认 | §7 撞击模式 |
| 边缘不反复触发 | 停在边缘 20 个事件只回绕 1 次；沿底边滑动 60 个样本不再回绕；离开边缘再回来正常回绕；角落换边仍是新到达 | §6 §7 |
| 撞击测的是「到达时的速度」 | **减速冲边不触发**：4000→250 px/s 在 1/2/4/8 ms 报点率下都不回绕；6000→150 在区间 50/100/150/250 下都不回绕（旧实现整段平均会误判，区间越宽越严重）；匀速 3000 px/s 在 1000Hz–125Hz 全部回绕，匀速 900 px/s 全部拒绝 | §7 撞击模式 |
| 排队投递不能抬高读数 | 20px/事件、投递间隔 500µs（纸面 40000 px/s）、驱动间隔 8ms（真实 2500 px/s）：带 `osMs` 读出 2500 并回绕；不带给 20000（上限） | §7 撞击模式 |
| 读数上限 | 99px/1ms 之类的极端输入，`measuredPps` 不会超过 `kImpactSpeedMaxPps`（20000），且不为 0 | §7 撞击模式 |
| 驱动 tick 只能降低速度 | `osMs` 跨 32 位回绕（0xFFFFFFF0 → 0x10）时窗口仍为正数，读数 >0 且 ≤ 上限 | §7 撞击模式 |
| 两个窗口是真设置 | `tuningOverlay` / `dataWindow` 经 `RawSettings` 往返保持；部分更新不会清掉它们 | §7 |
| 布局 fixture | `tests/layouts/*.json` 六个合成布局逐条断言 | §5 |

fixture 是**合成几何**，不是真机采集，因此可安全提交（不含设备名、用户名、机器名）。
真机采集用 `tools/capture_layout.ps1`（默认匿名化设备名），采集结果是否需要提交由
作者按隐私判断决定。

## L2 — 假后端集成

钩子回调与配置投递在不装真实钩子的前提下可测：

| 检查 | 对应 | 状态 |
|---|---|---|
| 配置经 `WM_APP_SETTINGS` 投递后在钩子线程生效（无锁热路径） | §8 | 由 L1 的设置合并用例 + 代码结构保证；无独立自动用例 |
| `LLMHF_INJECTED` 事件被观察但不回绕 | §4 | **未自动化**（需要真实钩子或事件注入） |
| `SetCursorPos` 失败时不吞事件 | §10 | **未自动化**（需要故障注入） |

## L3 — 真实进程协议往返（验收门槛）

`tests/protocol_roundtrip.py` 用真实 `cursorwrap.exe` 走 stdio JSON-RPC。

```powershell
python tests/protocol_roundtrip.py
```

2026-11-14 实跑：**123 项检查全部通过**，进程退出码 0。

| 检查 | 对应 |
|---|---|
| `initialize` 无 error、`protocolVersion == "1"` | §8 |
| 空查询返回**多行设置列表**（≥5 行），首行 `id == cursorwrap.status` | §3 |
| 状态行只有 `toggleEnabled` 一个动作（其他设置各占一行） | §3 |
| **清单 12 个设置逐项都有对应行**（读 `plugin.json` 的 `settings`，逐键核对列表里的行 id） | §3 §7 |
| 下钻行（speed/band/activation/guard/edge/tol/drag/single/tuner/data）带 `setQuery` | §3 |
| 带文字查询仍只返回一行（查询无副作用） | §3 |
| `impact` 打开子菜单（首行 `← 返回`），`impact on/off` 直接设值 | §3 |
| `speed 3500` 打开速度子菜单，状态行副标题含 `3500` | §3 |
| `drag` / `single` 子菜单可开可关，列表行跟着变 | §3 |
| `tune` 打开调参子菜单（开关 + 区间 / 门槛 / 防震 / 阈值四个下钻），`tune on/off` 直接开关 | §7 |
| `data` 打开数据窗口子菜单（开关 / 跳过计数 / 钩子健康），`data on/off` 直接开关，列表行与子菜单同步；`window` 是别名 | §8 §9 |
| 诊断含按原因跳过计数与钩子健康（`skipped …` / `hook responsive`） | §9 |
| `trace` 返回轨迹行，`copy` 字段带 CSV 表头 | §7 |
| `best` 打开建议子菜单；没有轨迹时返回「数据不足」行而不是编造数字 | §7 |
| `clear` 被接受且后续查询正常 | §7 |
| `cycleMode` 三次报告三种不同模式，每次 `replace` + `notify` | §3 |
| `toggleEnabled` 生效，替换后的状态行显示「已关闭」 | §3 |
| `copyDiagnostics` 产出一条 `copy`，内容含 mode/activation/monitors/outer edges/wraps | §3 |
| 未知动作返回 `valid: false` 的行，不假装成功 | §10 |
| `settingsChanged` 合法值立即反映在状态行 | §7 |
| `settingsChanged` 越界值不破坏后续查询 | §7 |
| 无对应 queryId 的 `cancel` 不影响后续查询 | §8 |
| `shutdown` 无 error，进程退出码 0 | §8 |

**这不是宿主集成**：它直接驱动 exe 的 stdio，不经过 Win顺 的插件装载、权限确认、
超时与重启逻辑。宿主集成见 L4 第 1 项。

## L3.5 — 免宿主 / 免真鼠标的 GUI + 逻辑场景（`cw_devshell`）

`cw_tests` 直接调用 `WrapEngine`，测不到 GUI；真机探针要移动真实鼠标，在远程桌面
下 `SetCursorPos` 会返回 `FALSE`，而且用户自己一动鼠标整轮就作废。`cw_devshell`
填这个空档：它**链接插件发布的同一批源文件**（`wrap_logic.cpp`、`wrap_topology.cpp`、
`overlay.cpp`、`data_window.cpp`、`settings_store.cpp`），只把「输入」换成脚本化的
合成样本，因此**不移动真实光标**，可以在 RDP 会话里跑，也可以边工作边跑。

```powershell
cmake --build build --target cw_devshell
python tests/devshell_scenarios.py            # 跑全部场景
python tests/devshell_scenarios.py --list     # 看覆盖了什么
build\cw_devshell.exe diag                    # 打印真实布局与外边缘
build\cw_devshell.exe gui                     # 真窗口，键盘 1/2/3 驱动，q 退出
```

| 场景 | 覆盖 |
|---|---|
| `impact-speed.txt` | 用户的两次投诉：减速冲边**不**回绕、快甩**要**回绕、慢漂拒绝；读数不超过 20000 px/s 上限；单屏右边缘落点为 `x=1` |
| `gui-layout.txt` | 双屏 `[-2560,0..0,1440;0,0..2560,1440]`：外边缘回绕到对侧最外边缘、**接缝（内边）永不回绕**、`vertical` 模式只回绕上下；两个窗口可在运行中开关 |
| `windows.txt` | 窗口由**设置**驱动（不是另一个开关）；两个窗口互相独立；关掉后不残留；无窗口时引擎照常回绕 |
| `visual-demo.txt` | 给窗口喂两簇到达速度，供人肉眼检查直方图 / 读数 / 决策日志（配合 `tools/devshell/capture.ps1` 截图） |
| `wrap-smoothness.txt` | 普通模式（关撞击）的「丝滑」：四条边落点精确值（右→`left+1`、左→`right-1`、上→`bottom-1`、下→`top+1`）、横跨接缝时纵坐标零漂移、连续 `sweep` 只回绕一次、四个方向都落在**工作区**内（≥1032 的行永不作为落点）、任务栏本身仍可移入/点击 |
| `acceleration-across-rates.txt` | 加速度读数不随回报率变化：同一 500→8000 px/s 甩腕在 125/500/1000 Hz 读到 3458/3200/3600 px/s²；匀速恒为 0；减速为负；加速度窗口不再污染速度窗口 |
| `gate-off-churn.txt` | 关闭撞击后连续跨越不得频繁触发：贴边保持、沿边滑动都只回绕一次，离开后可重新触发 |
| `acceleration.txt` | 加速度条件：匀速拒绝 / 真加速回绕 / 减速拒绝 / 门槛可调（500 通过、3000 拒绝同一轻微甩腕） |
| `regressions.txt` | 历次修复的行为回归（单屏、守卫、模式过滤、安置点） |
| `dual-1440p.txt` | 用户的真实桌面（2 × 2560×1440，主屏带任务栏）：含负坐标的精确落点，纵向环两端都避开任务栏 |
| `teardown.txt` | 窗口与线程收尾无残留 |
| `accel-fail-closed.txt` | 加速度门槛为 fail-closed：稳态 12000/16000 px/s、窄带（50/100/150/400）与刹车动作即使不可测量也一律拒绝，而真实 500->9000 px/s 甩动仍然穿越，`accel=0` 时门槛关闭 |

场景文件一行一条命令，`#` 起注释：`monitor`（可带工作区：
`l,t,r,b,workL,workT,workR,workB`，用来声明任务栏）、`set`、`move`、`stroke`、
`brake`、`hand`（按真实回报率线性加速并做整数像素量化）、`sweep`（把回绕结果反馈
给下一次采样，像真手一样连续移动）、`modifier`、`drag`、`wait`、`show`、`dump`、
`expect wrap|no-wrap|speed>=N|speed<N|accel>=N|accel<N|at <x> <y>`。
期望没满足时退出码非 0。

**它能证明什么 / 不能证明什么**：能证明判定逻辑与 GUI 的**行为**（画在哪些边、
读数是几、哪条门槛拒绝、窗口随设置开关）；不能证明真机上的**手感**、真实钩子的
安装/重连、以及 `SetCursorPos` 失败路径——那些仍然只有 L4 覆盖。

## L4 — 真机交互 E2E（Windows 11，Win顺 0.22.0+）

需真实桌面与至少两块屏。**单屏（含远程桌面单屏）部分已实测**（见下），多屏部分全部未执行。

### 已实测（远程桌面单屏，2026-11-13）

`tests/repeat_push_probe.py` 用真实 `SetCursorPos` + 真实钩子驱动真实插件进程，
**以插件自己的计数器为判据**（`delta_wraps`），不猜光标位置：

- [x] 单虚拟屏 1920×1080，向右推到边缘 → `delta_wraps=1`，落点 `x=1`
- [x] 向左推到边缘 → `delta_wraps=1`，落点 `x=1918`
- [x] 6 轮连续推送全部回绕，`y_stable=True`（未被外力打断）
- [x] 注入事件不破坏远程输入：`injected_seen=16` 时 `delta_wraps=1`
- [x] 观察钩子确认插件自己的 `SetCursorPos` 不带 `LLMHF_INJECTED`（`flags=0x0`），不会被自己的钩子拒绝
- [x] 进程干净退出 0

证据：`tests/layouts/rdp-wrap-evidence.json`。

### ⚠ 已撤回的结论（`live_wrap_probe.py`）

早期声称"5/6 通过"的判据说 `cursor_pos() != target` 即视为回绕成功。这个判据是错的：
推送被 Windows 在边界上夹住（`after=[1888,540]`，而命令值 1919）也会满足它。
`tests/layouts/live-wrap-evidence.json` 因此**不能作为回绕证据**，该文件的结论全部作废，
仅保留 `live-single.json` 作为布局 fixture。判据必须是插件计数器 + 落点坐标。

### 未执行（多屏）

- [ ] 打包 `.wsplugin` → 宿主「本地安装」→ 启用 → 权限确认只在 `ui` 上出现
- [ ] `cursorwrap` 留空：状态行屏数与系统显示设置一致
- [ ] 双屏横排：从最右屏右边缘向右 → 光标出现在最左屏左边缘
- [ ] 双屏竖排：从下屏下边缘向下 → 光标出现在上屏上边缘
- [ ] 屏间相邻边（内边）拖动 → Windows 原生跨屏，插件不介入、不闪回
- [ ] 拖窗口到边缘 → 不回绕（`disableDuringDrag`）；关掉该项后回绕
- [ ] `activationMode=ctrl`：不按 Ctrl 时到边缘不回绕；按住 Ctrl 才回绕；
      同时按 Ctrl 滚轮 → 任务栏音量插件工作，本插件不干扰
- [ ] `activationMode=shift` 同理
- [ ] `wrapMode=vertical`：左右边缘不回绕、上下边缘回绕
- [ ] `wrapMode=horizontal`：上下边缘不回绕、左右边缘回绕
- [ ] `disableOnSingleMonitor=true`：拔掉一块屏后不回绕
- [ ] 拔出/插回显示器 → 拓扑在约 0.5s 后重建，回绕方向随之正确
- [ ] 布局有缝隙（显示设置里两块屏未贴齐）→ 诊断里列出缝隙，行为可解释
- [ ] 远程桌面会话 → 单屏语义（已实测，见上）；断开重连期间的 `WM_DISPLAYCHANGE` 不得清空拓扑
- [ ] 连续 30 分钟把光标推过边缘 → 无内存/句柄增长，无卡顿
- [ ] 中英文 locale 下状态行与通知文案正确
- [ ] `copyDiagnostics` 粘贴内容不含窗口标题、文件名、用户名

## L5 — 标定与发布

| 项 | 内容 |
|---|---|
| 阈值标定 | 用 `tools/capture_layout.ps1` 采集真机布局 → 加进 `tests/layouts/` 回归；据此确认 50px 邻接容差与 50px 防震ign半径在目标布局上合适 |
| 打包 | `python tools/package.py` 产出 `.wsplugin` + `.sha256` + `release-notes.md`；`--tag` 校验版本 |
| 发布校验 | `python tools/ci_release.py dist`（本地）与 CI 的 verify → build → publish |
| 文档 | README 双语、CHANGELOG `[0.2.0]`、`docs/spec*`、`docs/tests*` 互链校验 |
| 图标 | `winshun-plugin-icons` 生成 `app.ico` 与 `.rc`，登记 `plugins.json` |

## 追溯矩阵

| spec 章节 | 覆盖它的测试 |
|---|---|
| §3 命令与动作 | L1 诊断结构、L3 全部动作检查、L4 第 2 项 |
| §4 手势与吞事件 | L1 模式/激活用例、L2 注入事件（未自动化）、L4 第 3–8 项 |
| §5 拓扑算法 | L1 拓扑组 + fixture + `nearestMonitor`、L5 阈值标定 |
| §6 防震荡与方向 | L1 状态机与角点用例、L4 第 3–4 项 |
| §7 设置 | L1 设置解析/类型混淆/两个窗口设置往返、L3 settingsChanged、L4 第 6–11 项 |
| §8 架构与协议 | L3 全部、L4 第 1 项、数据窗口子菜单（L3） |
| §9 数据 | L0 清单、L1 按原因计数 / 决策日志 / `lastSample`、L3 诊断与 `data` 子菜单 |
| §10 失败处理 | L1 空/退化/极端坐标、L1 `rdp_*` 空枚举与单屏用例、L2 故障注入（未自动化）、L4 第 12–15 项 |
| §11 权限 | L0 清单检查、L4 第 1 项 |

## 验收标准

- L1 全绿（当前 155 用例 + 6 布局 fixture，全部是合成几何；`live-single.json` 只被
  探针读取，不进 L1）
- L3 全通过（当前 119/119）
- L3.5 场景全通过（当前 12 个场景，`python tests/devshell_scenarios.py`）
- L0 静态校验通过
- L4 单屏与远程桌面单屏部分已实测通过；**多屏部分全部勾选后**才可宣布多屏可发布，
  当前属发布阻塞项

## 没测的

- L2 的注入事件与 `SetCursorPos` 失败路径没有自动化用例（需要真实钩子或故障注入）
- L4 多屏部分未执行：多屏手感、热插拔、拖拽抑制、独占全屏、高对比度、
  放大镜都没有在真机上按验收清单逐项验证
- 远程桌面的断开重连（`WM_DISPLAYCHANGE` + 空枚举路径）只有单测覆盖，没有在真实
  会话切换中观察过 `rebuildRefused`
- 看门狗的重装路径从未在真机上触发过（只有代码审查与数据窗口里的间接观察）
- 数据窗口只在一次真机渲染中检查过内容，混合 DPI 布局未验证
- `edgeThresholdPx` 设置项已接入判定（默认 1px），但只有合成几何验证，没有真机手感验证
- 自动化推鼠标会被用户自己的鼠标打断：若 `y_stable=False` 该轮结果作废，需重跑
