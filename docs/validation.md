# v0.1.0 实现与验证记录

[English](validation.en.md)

## 问题是什么

需要一个多显示器光标回绕插件：把光标推到布局最外层的外边时，从对边进来。参考实现
是 PowerToys CursorWrap（C++ 模块接口 + `MonitorTopology` 边拓扑）。Win顺 侧没有等价
能力。

## 为什么

把光标从最右推到最左需要长距离拖动；PowerToys 的做法是用边拓扑算出「目的边」，命中
后 `SetCursorPos` 并吞掉那一条移动消息。这个做法可以直接移植，而且它的算法部分
（边映射、内外边分类、非重叠区投影、防震荡）不依赖任何 Windows API，适合做成
Win顺 插件里的纯核心。

## 改了什么

新仓库 `ra9fael.cursorwrap`，v0.1.0：

| 文件 | 内容 |
|---|---|
| `src/wrap_topology.{h,cpp}` | 边拓扑纯核心（移植自 `MonitorTopology`）：边映射、邻接容差、内外边分类、对边查找 + 非重叠投影、缝隙诊断 |
| `src/wrap_logic.{h,cpp}` | 引擎状态机：模式过滤、激活键、拖拽抑制、单屏禁用、防震荡半径、角点方向优先；设置解析与钳制 |
| `src/mouse_hook.{h,cpp}` | 独立线程 `WH_MOUSE_LL`；回绕时 `SetCursorPos` + 吞掉该条移动；注入事件只观察 |
| `src/monitor_watcher.{h,cpp}` | message-only 窗口，500ms 防抖重建拓扑 |
| `src/main.cpp` | `winshun::Plugin` 派生；`cursorwrap` 关键字 + 三个动作 |
| `plugin.json` | resident / eager / `ui` 权限 / 7 个设置项 |
| `tests/` | 43 个单测、6 个合成布局 fixture、32 项协议往返、L0 静态校验 |
| `docs/` | 双语 spec 与 tests（含"没测的"） |

## 实测

### 构建

MinGW-w64 GCC 13.2 / CMake 3.29 / Ninja Release：

```
[11/11] Linking CXX executable .../cursorwrap.exe
```

`cursorwrap.exe` 898 466 字节，静态链接 libstdc++/libgcc/winpthread。

### L1 单元测试

```
$ ctest --test-dir build -C Release
1/1 Test #1: cw_tests .........................   Passed    0.03 sec
100% tests passed, 0 tests failed out of 1

$ .\build\cw_tests.exe .\tests\layouts
[FIXT ] 6 layout fixture file(s) from ./tests/layouts
all tests passed (0 failures)
```

102 个用例全绿，覆盖：单屏/双屏横/双屏竖/L 形三屏/非重叠投影/三屏并排/混合 DPI/
负坐标/极端坐标/零面积屏/邻接容差/缝隙诊断/三种模式/三种激活方式/拖拽抑制/单屏禁用/
防震荡进出/角点方向/设置钳制与类型混淆/NaN 与 Inf/诊断结构/阈值带宽/撞击模式
（慢速漂移不触发、快速撞击触发、垂直分量判定、时间戳缺失判负、回绕后历史清零、
门槛边界值）。

### L3 协议往返（真实进程）

```
$ python tests/protocol_roundtrip.py
...
all 103 protocol checks passed
```

覆盖 `initialize → query(空/带文字) → action(cycleMode×3 / toggleEnabled /
copyDiagnostics / 未知) → settingsChanged(合法/越界) → cancel(未知 id) → shutdown`，
进程退出码 0。

### L0 静态校验与打包

```
$ python tests/validate.py --package-dir dist
static validation passed

$ python -c "... jsonschema.validate(manifest, schema) ..."
full schema validation: PASS

$ python tools/package.py --tag v0.1.0
ra9fael.cursorwrap-0.1.0.wsplugin: 47 files, 1220232 bytes,
sha256 f44686111026e5fb6ecd618c474649b84cc278914953f6b3e248b824d7202722
```

包内容核对：47 个文件 = git 跟踪文件 + `cursorwrap.exe`；`plugin.json` 在根部；
无 `build/`、`dist/`、`.git`、日志。双语 release notes 见
`docs/release-notes/v0.1.0.md`（四段：下载 / 新功能 / 改进与修复 / 插件库）。

注：文档后续又补了 release notes 与 README 链接，最终包为 47 文件 / 1 222 366 字节 /
sha256 `c6342986…b6c9cde`（`dist/*.sha256` 为准）。两轮包都通过了
`tests/validate.py --package-dir` 与 `tools/ci_release.py` 校验；宿主安装验证用的是
第一轮包（结构相同，仅文档字节数不同）。

### 真实宿主集成

把 `plugin.json` / `cursorwrap.exe` / `README.md` / `LICENSE` 复制到
`%LOCALAPPDATA%\WinShun\plugins\ra9fael.cursorwrap\`，在
`%APPDATA%\WinShun\WinShun.ini` 加 `[Plugin.ra9fael.cursorwrap]` /
`Enabled=true` / `Confirmed=ui`，重启宿主后 `plugins.log`：

```
2026-11-10 04:47:52.181 ra9fael.cursorwrap: 启动 ...\ra9fael.cursorwrap\cursorwrap.exe
2026-11-10 04:47:52.383 ra9fael.cursorwrap [info] cursorwrap: hook installed, watcher started, 1 monitor(s)
```

- 钩子安装成功、显示器监听线程启动、枚举到 1 块屏，与真机一致
- `--query "cursorwrap"` 打开输入框后无错误行，常驻进程 **6.6 MB**（上限 64 MB）
- `--quit` 时干净退出：`退出（代码 0）`，无超时、无崩溃、无重启
- 二次启动（`--background`）后再次正常加载，无残留

**未做**：GUI 里启用开关的手动点击、权限确认弹窗的目视核对（这两步是宿主 UI，
本轮通过直接改 `WinShun.ini` 完成）。

### 从 .wsplugin 包安装（第二轮，最终包）

最终打包 `dist/ra9fael.cursorwrap-0.1.0.wsplugin`（47 文件，1 220 232 字节，
sha256 `f4468611…7202722`，`--tag v0.1.0` 校验通过）后，把**包内容**解压到宿主插件
目录、按上面同样的方式启用并重启宿主：
```
2026-11-10 16:27:20.702 ra9fael.cursorwrap: 启动 ...\ra9fael.cursorwrap\cursorwrap.exe
2026-11-10 16:27:20.910 ra9fael.cursorwrap [info] cursorwrap: hook installed, watcher started, 1 monitor(s)
```

- 包内的 exe 与源码构建一致，宿主接受包结构（`plugin.json` 在根部）
- `--query "cursorwrap"` 后常驻进程 **6.7 MB**，无错误行
- 验证后已移除测试安装、还原 `WinShun.ini`，宿主恢复正常运行

### 真机手势（单屏，L4 部分）

`tests/live_wrap_probe.py` 用真实 `SetCursorPos` 驱动真实钩子：探针自己装一个
`WH_MOUSE_LL` 观察钩子记录事件，再向屏幕边缘推光标，读回光标实际落点。真机布局
1920×1080（`tests/layouts/live-single.json`）：

| 用例 | 起点 | 推向 | 实际落点 | 结论 |
|---|---|---|---|---|
| 推右边缘 | (960, 540) | (1919, 540) | **(0, 536)** | 回绕到左边缘 ✓ |
| 推左边缘 | (960, 540) | (0, 540) | **(1919, 540)** | 回绕到右边缘 ✓ |
| 屏幕中间 | (960, 540) | (960, 540) | (960, 540) | 不回绕 ✓ |
| 退出 | — | — | 退出码 0 | 干净 ✓ |

观察钩子共收到 27 条真实移动事件，全部 `LLMHF_INJECTED=0`（探针的 `SetCursorPos`
在钩子看来不是注入事件——这正是插件自己 `SetCursorPos` 时的真实环境，说明插件的
回绕落点不会被自己的钩子当成注入而拒绝）。证据文件：
`tests/layouts/live-wrap-evidence.json`。

**这证明单屏 wrap 在真机上工作**：插件进程、真实钩子、真实 `SetCursorPos`、
防震荡半径全部在真实路径上跑通。仍未覆盖的是多屏几何与手感（见"没测的"）。

### 撞击模式真机验证（L4 单屏，2026-11-10 第二轮；测速区间版 2026-11-11 第三轮）

`tests/live_wrap_probe.py` 扩展后对比门槛关闭/开启、快推/慢漂/刹车/瞬移
（门槛 3000 px/s，探针的步进时序换算：24 步 ×5ms 快推、×100ms 慢漂、×120ms 刹车）：

| 用例 | 推向 | 实际落点 | 结论 |
|---|---|---|---|
| 门槛关闭 + 快推 | (1919, 540) | (0, 511) | 0.1 行为不变 ✓ |
| 门槛开启 + 快推 ≈5300px/s | (1919, 540) | (0, 540) | 回绕 ✓ |
| 门槛开启 + 慢漂 ≈800px/s | (1919, 540) | (1919, 540) | **不回绕** ✓ |
| 门槛开启 + 刹车（远处快、带内慢 ≈670px/s） | (1919, 540) | (1919, 540) | **不回绕**（区间截断）✓ |
| 门槛开启 + 快推左边缘 | (0, 540) | (1919, 540) | 回绕 ✓ |
| 门槛开启 + 单次瞬移 | (1919, 540) | (1919, 540) | **不回绕**（fail closed）✓ |

「刹车」用例是测速区间的直接真机证据：整段行程平均速度接近门槛，但带内部分
（≈150px）明显低于门槛，按区间截断语义正确判负——旧的整窗口平均算法在这种
场景下会误回绕。

单元测试 24 项（`test_impact_gate.cpp`，其中 12 项为测速区间与去抖新增）+ 协议往返
诊断检查同步通过。注意探针第一次跑全部 `observed_moves: 0`——
原因是 `settingsChanged` 通知后没有泵消息钩子就收不到事件，补一个 `pump(50)`
后恢复；这个坑写进探针注释，避免下次再踩。

### 调参覆盖层真机验证（L4，2026-11-12 双屏）

块截图与窗口枚举（探针直接跑真实 `Overlay` 类，不经过宿主；**不移动鼠标**）：

| 检查 | 结果 |
|---|---|
| 窗口数 = 外边缘数 | 双屏 2560×1440×2，枚举到 **6 个** 可见的 `WinShunCursorWrapTuner` 窗口 |
| 只画外边缘 | 窗口矩形为 `[-2560,0..0,150]`、`[-2560,1290..0,1440]`、`[-2560,0..-2410,1440]`、`[0,0..2560,150]`、`[0,1290..2560,1440]`、`[2410,0..2560,1440]`——**内边缘（x=0、x=2560 那条接缝）没有窗口**，中间区域 0 个窗口 |
| 逐像素 alpha 生效 | 截图中边缘线为清晰 2px 线、内侧为半透明带，桌面透出（不是整屏遮罩） |
| 内存恒定 | 工作集 24,076 KB，持续绘制 6 秒后仍是 24,076 KB（旧版按帧建 4K DIB 会崩） |
| 关闭干净 | `stop()` 后枚举到 0 个窗口 |
| 轨迹直方图 | 注入 28 条到达（14 慢 ≈600 px/s + 14 快 ≈5000 px/s），面板画出两簇，`gate 2000` 与 `best 2800` 落在两簇之间的空档；标题 `28 hits · 14 wrap` |
| 面板不被任务栏切 | 按 `rcWork` 定位后整块可见 |
| 面板持续更新（定时器） | 先注入 12 条慢速到达 → 直方图一簇；再注入 12 条快速 → 变成两簇、`best 2800` 竖线出现、标题 `24 hits · 12 wrap`（重建边缘条后定时器重新挂上，未冻结） |

**这证明覆盖层不再是整屏遮罩**：中间区域没有任何窗口，只有外边缘的窄条。

### 撞击测速稀释缺陷（真机复现 → 修复）

用户报告「撞击模式完全失效」，用真实 125Hz 时序复现：手撞到边缘后 Windows 钳位
指针，后续事件坐标重复，这些重复样本被计入平均速度——2000 px/s 的推手在到达边
之后依次测得 1986 → 1787 → 1625 → … → 0，门槛 2000 一个都过不了。修复后
（剥掉钳位尾部 + 最后一步量到最后一个自由样本）同一个 2100 px/s 用例稳定通过，
贴边不动（只有重复样本）仍然 fail closed。4 个回归用例覆盖。

### 收尾清理

- 已从 `%LOCALAPPDATA%\WinShun\plugins\` 移除测试安装与 `.data` 目录
- 已用备份 `WinShun.ini.backup` 还原 `WinShun.ini`
- `build/`、`dist/`、`schema-cache/` 在 `.gitignore` 内；`git status --short` 干净

## 没测的

- **多显示器真机行为全部未验证**：本机只有一块显示器。双屏横排/竖排/L 形、内边不介入、
  热插拔重建、缝隙布局、`disableOnSingleMonitor` 的实际手感都只有合成几何的单测覆盖。
  **单屏回绕已有真机证据**（见上节 `live_wrap_probe.py`），不在未验证之列。
- **真实鼠标手势**：`SetCursorPos` 落点、吞事件后是否有跳帧、拖拽抑制手感、与
  「按住 Ctrl 滚轮」的音量插件同时使用时的互相干扰，都没有在真机上试过。
- **远程会话、独占全屏、高对比度、放大镜**未验证。
- **主机 UI 路径**：从「添加插件 → 压缩包」安装 `.wsplugin` 并点击启用开关的流程
  没有走 UI，是直接改配置文件完成的。
- `wrapMode` / `activationMode` / `enable` 的切换在真实手势下是否即时生效，只验证了
  状态行文案。
- 锁屏与安全桌面下的行为（预期无事件、什么都不做）。
