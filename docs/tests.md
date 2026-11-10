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
| 打包物含 `plugin.json` / `cursorwrap.exe` / `README.md` / `LICENSE`，无 `build/`、`dist/`、`.git` | §8 | `tools/package.py` + `tools/ci_release.py` |
| 双语文档锚点互链（`docs/spec*.md`、`docs/tests*.md`、README、CHANGELOG） | — | 通过 |

## L1 — 纯逻辑单测（`cw_tests`，ctest 目标，80 用例 + 7 布局 fixture）

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
| 诊断结构 | 屏数/外边数/钩子标志/缝隙文本 | §3 |
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

2026-11-10 实跑：**63 项检查全部通过**，进程退出码 0。

| 检查 | 对应 |
|---|---|
| `initialize` 无 error、`protocolVersion == "1"` | §8 |
| 空查询返回**多行设置列表**（≥5 行），首行 `id == cursorwrap.status`，含 10 个设置行 id | §3 |
| 状态行只有 `toggleEnabled` 一个动作（其他设置各占一行） | §3 |
| 下钻行（speed/band/activation/guard/edge/tol）带 `setQuery` | §3 |
| 带文字查询仍只返回一行（查询无副作用） | §3 |
| `impact` 打开子菜单（首行 `← 返回`），`impact on/off` 直接设值 | §3 |
| `speed 3500` 打开速度子菜单，状态行副标题含 `3500` | §3 |
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

## L4 — 真机交互 E2E（Windows 11，Win顺 0.22.0+）

需真实桌面与至少两块屏。**单屏部分已实测**（见下），多屏部分全部未执行。

### 已实测（单屏，2026-11-10）

`tests/live_wrap_probe.py` 用真实 `SetCursorPos` + 真实钩子驱动真实插件进程：

- [x] 推到右边缘 → 光标落在左边缘 (0, 536)
- [x] 推到左边缘 → 光标落在右边缘 (1919, 540)
- [x] 屏幕中间 → 不回绕
- [x] 进程干净退出 0
- [x] 观察钩子确认插件自己的 `SetCursorPos` 不带 `LLMHF_INJECTED`，不会被自己的钩子拒绝
- [x] 撞击模式（门槛 3000px/s）：快推 ≈5300px/s → 回绕；慢漂 ≈800px/s → 不回绕；
      单次瞬移 → 不回绕（fail closed）；门槛关闭后行为与 0.1 一致

证据：`tests/layouts/live-wrap-evidence.json`、`tests/layouts/live-single.json`。

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
- [ ] 远程桌面会话 → 单屏语义，不误报
- [ ] 连续 30 分钟把光标推过边缘 → 无内存/句柄增长，无卡顿
- [ ] 中英文 locale 下状态行与通知文案正确
- [ ] `copyDiagnostics` 粘贴内容不含窗口标题、文件名、用户名

## L5 — 标定与发布

| 项 | 内容 |
|---|---|
| 阈值标定 | 用 `tools/capture_layout.ps1` 采集真机布局 → 加进 `tests/layouts/` 回归；据此确认 50px 邻接容差与 50px 防震ign半径在目标布局上合适 |
| 打包 | `python tools/package.py` 产出 `.wsplugin` + `.sha256` + `release-notes.md`；`--tag` 校验版本 |
| 发布校验 | `python tools/ci_release.py dist`（本地）与 CI 的 verify → build → publish |
| 文档 | README 双语、CHANGELOG `[0.1.0]`、`docs/spec*`、`docs/tests*` 互链校验 |
| 图标 | `winshun-plugin-icons` 生成 `app.ico` 与 `.rc`，登记 `plugins.json` |

## 追溯矩阵

| spec 章节 | 覆盖它的测试 |
|---|---|
| §3 命令与动作 | L1 诊断结构、L3 全部动作检查、L4 第 2 项 |
| §4 手势与吞事件 | L1 模式/激活用例、L2 注入事件（未自动化）、L4 第 3–8 项 |
| §5 拓扑算法 | L1 拓扑组 + fixture、L5 阈值标定 |
| §6 防震荡与方向 | L1 状态机与角点用例、L4 第 3–4 项 |
| §7 设置 | L1 设置解析/类型混淆、L3 settingsChanged、L4 第 6–11 项 |
| §8 架构与协议 | L3 全部、L4 第 1 项 |
| §9 数据 | L0 清单、L4 第 17 项 |
| §10 失败处理 | L1 空/退化/极端坐标、L2 故障注入（未自动化）、L4 第 12–15 项 |
| §11 权限 | L0 清单检查、L4 第 1 项 |

## 验收标准

- L1 全绿（当前 80 用例 + 7 fixture，含真机采集的 `live-single.json`）
- L3 全通过（当前 32/32）
- L0 静态校验通过
- L4 单屏部分已实测通过；**多屏部分全部勾选后**才可宣布 v0.1.0 多屏可发布，
  当前属发布阻塞项

## 没测的

- L2 的注入事件与 `SetCursorPos` 失败路径没有自动化用例（需要真实钩子或故障注入）
- L4 全部未执行：本机只有一块显示器，多屏手感、热插拔、拖拽抑制、远程会话、
  独占全屏、高对比度、放大镜都没有在真机上验证
- `edgeThresholdPx` 设置项已接入判定（默认 1px），但只有合成几何验证，没有真机手感验证
