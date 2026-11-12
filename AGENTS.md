# 仓库约定

[English](AGENTS.en.md)

提交标题和正文采用英文 Angular 格式：`type(scope): imperative English summary`，
破坏性变更使用 `BREAKING CHANGE:` 正文。一次提交只做一件事。

## 版本与标签

`plugin.json` 的 `version` 是唯一版本来源，release tag 为 `v<version>`（与
`tools/package.py --tag` 校验的值一致）。按语义化版本：

| 场景 | 版本位 | 例子 |
|---|---|---|
| 新增用户可见的功能面、新增设置项、新增命令或窗口 | **minor** | 0.1.0 → 0.2.0 |
| 修 bug、改措辞、内部重构、只加测试 | **patch** | 0.2.0 → 0.2.1 |
| 破坏兼容的改动（删设置键、改 `plugin.json` 结构、要求更高 `minWinShun`） | **major** | 1.0.0 |

规矩：

1. **改动多的时候必须打标签**，不要攒着一起发。
2. 打标签前先把 `CHANGELOG.*` 的 `[Unreleased]` 收成 `## [<version>] - <YYYY-MM-DD>`，
   并新开一个空的 `## [Unreleased]`；`tests/validate.py` 会核对这个章节名与
   `plugin.json` 的版本一致，对不上就失败。
3. `python tools/package.py --tag v<version>` 打包（它同时产 `.sha256` 与
   release notes），再 `git tag -a v<version> -m "..."`。
4. 已发布的 Release 资产**永不替换**：发现问题就发新的 patch 版本。

Actions 公开入口只允许新版本 tag 推送与 `workflow_dispatch`，不得添加分支 push、
PR 或定时触发。tag 任务必须检查 `github.event.created && !github.event.deleted`。

手动验证接受源码 ref，`publish` 默认为 false；发布要求已有且匹配的版本 tag。
保留全部测试与清单、文档、包验证。仅发布任务拥有 `contents: write`，不得覆盖
已有 Release 资产。

## 低级鼠标钩子的红线

`WH_MOUSE_LL` 的回调里**不许有任何 I/O**——不写日志、不发通知、不抢锁、不写
stdout。Windows 侧看门狗超时（`LowLevelHooksTimeout`，默认 300ms）会**静默卸载**
钩子，而插件仍认为自己是好的。需要告知外界的事只置原子标志，由协议线程的合并
定时器统一发出。

`SetCursorPos` 成功后必须立刻把落点写进「钩子最后看到的位置」，否则看门狗会把
插件自己的回绕误判成钩子死亡。

## 时间戳的红线

取微秒**只能**用 `src/qpc_time.h` 的 `counterToUs()`。裸写
`QueryPerformanceCounter * 1'000'000 / freq` 会在开机约 10.7 天后溢出 `int64`
（`freq` 是 10MHz），之后每个时间戳都是负数，撞击门槛因此算出
`measured_pps = 0` 并拒绝所有回绕。这个缺陷只有「长期开机 + 开门槛」才暴露，
单测里的时钟都从很小的值起算、永远踩不到，所以**新加时钟相关用例必须用开机
数周量级的计数值**。

## 调试的边界

任何会移动真实光标、或安装真实全局钩子的排查手段（包括直接启动
`cursorwrap.exe` 做探针），**必须先征得用户同意**。编译、单测、协议往返、
静态校验都不需要征求。

验证证据按工作台规范（`winshun-dev/DEVELOPMENT.md`）：仓库内只放精选证据与
机器可读结果（≤5MB），原始素材不进本仓库；验证 README 必含"没测的"一节。
