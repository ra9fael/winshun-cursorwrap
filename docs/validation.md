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

43 个用例全绿，覆盖：单屏/双屏横/双屏竖/L 形三屏/非重叠投影/三屏并排/混合 DPI/
负坐标/极端坐标/零面积屏/邻接容差/缝隙诊断/三种模式/三种激活方式/拖拽抑制/单屏禁用/
防震荡进出/角点方向/设置钳制与类型混淆/NaN 与 Inf/诊断结构/阈值带宽。

### L3 协议往返（真实进程）

```
$ python tests/protocol_roundtrip.py
...
all 32 protocol checks passed
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

$ python tools/package.py
ra9fael.cursorwrap-0.1.0.wsplugin: <files> files, <bytes> bytes, sha256 <digest>
```

包内文件数与 SHA-256 每改一次文档就会变，所以这里不写死具体值：以本次构建后
`python tools/package.py` 的实际输出、以及 `dist/*.sha256` 为准。

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

### 收尾清理

- 已从 `%LOCALAPPDATA%\WinShun\plugins\` 移除测试安装与 `.data` 目录
- 已用备份 `WinShun.ini.backup` 还原 `WinShun.ini`
- `build/`、`dist/`、`schema-cache/` 在 `.gitignore` 内；`git status --short` 干净

## 没测的

- **多显示器真机行为全部未验证**：本机只有一块显示器。双屏横排/竖排/L 形、内边不介入、
  热插拔重建、缝隙布局、`disableOnSingleMonitor` 的实际手感都只有合成几何的单测覆盖。
- **真实鼠标手势**：`SetCursorPos` 落点、吞事件后是否有跳帧、拖拽抑制手感、与
  「按住 Ctrl 滚轮」的音量插件同时使用时的互相干扰，都没有在真机上试过。
- **远程会话、独占全屏、高对比度、放大镜**未验证。
- **主机 UI 路径**：从「添加插件 → 压缩包」安装 `.wsplugin` 并点击启用开关的流程
  没有走 UI，是直接改配置文件完成的。
- `wrapMode` / `activationMode` / `enable` 的切换在真实手势下是否即时生效，只验证了
  状态行文案。
- 锁屏与安全桌面下的行为（预期无事件、什么都不做）。
