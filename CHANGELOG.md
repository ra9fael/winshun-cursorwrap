# Changelog

[English](CHANGELOG.en.md)

All notable changes to this plugin. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and versions follow
[Semantic Versioning](https://semver.org/). The version in `plugin.json` is the
single source of truth; release tags are `v<version>`.

## [Unreleased]

### Added

- 搜索栏直接改设置：`cursorwrap impact`（撞击开关）、`cursorwrap speed 3000`
  （撞击门槛）、`cursorwrap mode vertical`（方向）、`cursorwrap ctrl/shift/always`
  （激活方式）、`cursorwrap guard/edge/tol <n>`（阈值）、`cursorwrap reset`
  （恢复宿主设置卡）。改动立即生效并持久化到插件数据目录
  （`WS_DATA_DIR\settings.json`，只存与默认不同的键）；宿主设置卡对未覆盖的键
  仍然权威，状态行注明来源。中英文关键词、数字越界夹入、未知词给提示行。
- 新增动作 `toggleImpact`（Shift+Enter 切换撞击模式；复制诊断移到 Ctrl+Enter）。
- 撞击模式（`impactGate` / `impactSpeedPps`）：开启后光标必须以足够速度撞到边缘才
  回绕，防止慢慢挪到边缘（比如去点任务栏）时误触发。速度按 100ms 窗口内垂直于
  边缘的位移计算（500–20000 px/s，默认 2000）；沿边缘平行滑动不算；测不出速度
  （时间戳缺失/倒退、程序化瞬移）判负不回绕。真机验证：快推 ≈5300px/s 回绕、
  慢漂 ≈800px/s 与单次瞬移不回绕。
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
