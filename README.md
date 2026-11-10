# ra9fael.cursorwrap — 多显示器光标回绕（Win顺 插件）

> **简体中文（默认）** · [English](README.en.md)

光标推到屏幕**外边**时，从**对边**重新出现。多显示器并排或上下叠放时，省掉横跨整个
桌面的长距离拖动。算法移植自 PowerToys CursorWrap（MIT）。

## 设计目的

多屏桌面上，把光标从最右边推到最左边要长距离拖动；很多工具用快捷键或热角（hot
corner）解决，都得额外记一个操作。本插件用的是「把光标推到屏幕边上」这个本来就会
做的动作：到了最外层的外边，就从布局的对边进来。

屏与屏**贴合**的那条边（内边）不参与回绕——那里 Windows 自己就会把光标送到相邻屏，
插件不插手，不会出现「闪回」。

## 用法

输入 `cursorwrap`（或 `wrap` / `环绕`）看状态，**在后面带词直接改设置**：

| 输入 | 效果 |
|---|---|
| `cursorwrap`（留空） | 状态行 |
| `cursorwrap impact` / `撞击` | 撞击模式开 ↔ 关 |
| `cursorwrap impact on` / `off` | 直接设撞击模式 |
| `cursorwrap speed 3000` / `速度 3000` | 撞击门槛（500–20000 px/s，设速度即开撞击） |
| `cursorwrap mode` / `模式` | 方向循环：双向 → 仅上下 → 仅左右 |
| `cursorwrap mode vertical` / `模式 上下` | 直接设方向 |
| `cursorwrap ctrl` / `shift` / `always` / `按住` / `总是` | 激活方式 |
| `cursorwrap guard 120` / `防震 120` | 防震荡半径（10–200 px） |
| `cursorwrap edge 5` / `边缘 5` | 边缘判定距离（1–20 px） |
| `cursorwrap tol 80` / `容差 80` | 邻接容差（10–200 px） |
| `cursorwrap reset` / `恢复` | 清掉搜索栏覆盖，宿主设置卡重新生效 |

改动**立即生效**，并持久化到插件数据目录（`settings.json`，只存与默认不同的键）；
宿主设置卡对没被覆盖的键仍然权威，状态行会注明「搜索栏覆盖中」。

| 键 | 效果 |
|---|---|
| Enter | 开启 / 关闭回绕 |
| Ctrl+Enter | 切换模式：双向 → 仅上下 → 仅左右 |
| Shift+Enter | 撞击模式开 ↔ 关 |
| Ctrl+Enter | 复制诊断信息（模式、屏数、外边数、计数、阈值、布局缝隙） |

## 设置

| 键 | 默认 | 说明 |
|---|---|---|
| `wrapMode` | both | 双向 / 仅上下 / 仅左右 |
| `activationMode` | always | 总是回绕 / 按住 Ctrl / 按住 Shift |
| `disableDuringDrag` | true | 按住左键拖动时不回绕 |
| `disableOnSingleMonitor` | false | 只有一块屏时禁用（默认仍回绕到本屏对边） |
| `edgeThresholdPx` | 1 | 1–20，离外边多近算到达 |
| `wrapGuardPx` | 50 | 10–200，回绕落点周围的防震荡半径 |
| `adjacencyTolerancePx` | 50 | 10–200，两条边差多少像素内算相邻（内边） |
| `impactGate` | false | 撞击模式：光标必须以足够速度撞到边缘才回绕；慢慢挪到边缘不触发 |
| `impactSpeedPps` | 2000 | 500–20000，撞击速度门槛（px/s，垂直于边缘、100ms 窗口） |

非法值一律回落到默认，不会报错。

## 权限说明

本插件只申请 `ui`（窗口与钩子）权限，因为需要：

- 一个低级鼠标钩子（`WH_MOUSE_LL`）来观察光标移动；
- 一个**不可见的** message-only 窗口来接收显示器变更通知。

它不申请管理员权限、不访问网络、不读写文件、不动剪贴板。光标坐标与显示器几何
**只在插件进程内使用**，不落盘、不出网。

宿主对 `ui` 只是**部分**强制，这一点请按你自己的判断接受或拒绝。

## 已知限制

- 独占全屏（exclusive fullscreen）的 DirectX 程序里外框可能不生效
- 若系统因 `LowLevelHooksTimeout` 把钩子摘掉，本版不会自动重装（状态行会显示
  「钩子未安装，仅显示状态」）；重新启用插件即可
- 远程桌面会话只有一块虚拟屏，走单屏语义
- 高对比度、放大镜等辅助功能未专门验证
- 与 PowerToys 原版的差异：没有「快捷键触发回绕」模式，也没有按进程自动启停

## 安装

1. 从 [Release](https://github.com/ra9fael/winshun-cursorwrap/releases/latest) 下载
   `ra9fael.cursorwrap-<version>.wsplugin`（可用旁边的 `.sha256` 核对：
   `Get-FileHash <文件> -Algorithm SHA256`）
2. Win顺 → **设置 → 插件 → 已安装 → 添加插件 → 压缩包**，选中该文件
3. 本地安装的插件**不会自动启用**，请在列表里打开开关；启用时确认 `ui` 权限

本插件是原生 exe 包，需要 Win顺 0.22.0 或更新（清单的 `minWinShun` 声明了这一点）。
Win顺 认出同一个 id 会询问是否替换旧版本，并保留插件的数据目录；卸载在
**设置 → 插件 → 已安装** 里移除。

**升级后需重新启动插件进程**（停用再启用，或重启 Win顺）——原生 exe 不走脚本热重载。

**手动安装**（不经过宿主界面）：把包内文件解到
`%LOCALAPPDATA%\WinShun\plugins\ra9fael.cursorwrap\`（`plugin.json` 必须在目录根部），
再在 `WinShun.ini` 加：

```ini
[Plugin.ra9fael.cursorwrap]
Enabled=true
Confirmed=ui
```

然后重启 Win顺。`Confirmed=ui` 表示你确认了 `ui` 权限；没有这一行，宿主会在启用时
弹确认。

## 从源码构建

```powershell
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

需要 CMake 3.16+、C++17 编译器（MinGW-w64 或 MSVC）。产物 `cursorwrap.exe` 落在
仓库根，与 `plugin.json` 同级（`runtime.program` 是相对路径）。GNU 分支会静态链接
C++ 运行时，目标机器不需要 MinGW。MSVC 构建用静态运行库（`/MT`），同样无外部 DLL。

## 测试

```powershell
ctest --test-dir build --output-on-failure       # L1：54 用例 + 7 布局 fixture
python tests/protocol_roundtrip.py               # L3：32 项协议往返检查
python tests/live_wrap_probe.py                  # L4 单屏：真实 SetCursorPos 手势
python tests/validate.py                         # L0：清单/双语文档/打包静态校验
```

设计说明见 [docs/spec.md](docs/spec.md)，测试规格见 [docs/tests.md](docs/tests.md)，
本轮验证证据见 [docs/validation.md](docs/validation.md)。

## 打包

```powershell
python tools/package.py            # dist/<id>-<version>.wsplugin + .sha256 + release-notes.md
python tools/package.py --tag v0.1.0   # 校验 tag 与 plugin.json 版本一致、CHANGELOG 有对应 section
```

包内容：git 跟踪的全部文件 + `cursorwrap.exe`，确定性 zip（固定时间戳、不压缩），
同一份输入永远得到同一个 SHA-256。发布流程（tag → CI verify/build/publish）见
[.github/workflows/release.yml](.github/workflows/release.yml) 与
[AGENTS.md](AGENTS.md)。双语 release notes 在
[docs/release-notes/v0.1.0.md](docs/release-notes/v0.1.0.md)。

## 许可证

MIT。算法移植自 PowerToys CursorWrap（Microsoft Corporation，MIT），
详见 [docs/spec.md](docs/spec.md) 第 5 节。

GUI 图标由 [winshun-plugin-icons](https://github.com/ra9fael/winshun-plugin-icons)
从 Phosphor 字形 `arrows-out-cardinal` 生成。
