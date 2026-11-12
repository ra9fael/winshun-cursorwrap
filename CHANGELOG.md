# Changelog

[English](CHANGELOG.en.md)

All notable changes to this plugin. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and versions follow
[Semantic Versioning](https://semver.org/). The version in `plugin.json` is the
single source of truth; release tags are `v<version>`.

## [Unreleased]

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
