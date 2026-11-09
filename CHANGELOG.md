# Changelog

[English](CHANGELOG.en.md)

All notable changes to this plugin. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and versions follow
[Semantic Versioning](https://semver.org/). The version in `plugin.json` is the
single source of truth; release tags are `v<version>`.

## [Unreleased]

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
