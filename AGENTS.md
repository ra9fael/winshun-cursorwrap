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

时间跨度**只能**用 `elapsedUs()`，它会同时看 `QueryPerformanceCounter`
（`Sample::tUs`，回调里读）和 `MSLLHOOKSTRUCT.time`（`Sample::osMs`，驱动产生
事件的 tick），取**较大**者。只用前者会被「排队投递」骗：Windows 把事件排队后
成批交给钩子，一批坐标相差真实距离的事件时间戳只差几微秒，读数上万（实测
真实 2500 px/s 读成 40000）。取较大者意味着排队只能让读数偏低——偏低即
fail closed，是安全方向。

## 撞击门槛的语义

门槛判的是**到达边缘那一刻的速度**，不是整段飞行的平均。用户的原话是
「我想要的是在边沿处加速才行」。所以 `approachSpeedPps` 从到达点往回走，
取「刚好够测出速度」的**最短**窗口。

两条配套约束，改动时别破坏：

- `impactBandPx` 是**硬限制**，区间外样本不参与测量。区间内可测行程不足 20px 时
  就是「测不出来」，按 fail closed 处理——把区间外样本拉进来等于把飞行段重新算进
  速度，正是本次修掉的缺陷。区间本身是用户可调项：粗采样下区间内不够时把区间调大
  （真机：区间 100 → 0 px/s，250 → 3587）。
- 指针撞到边缘时 Windows 会夹住它，到达那一步的位移被截短（实测 15px 对正常
  16–17px），会把真实速度拖到门槛以下。判据是「到达点在边缘线上且这一步短于
  前一步」，命中则改用最后一个自由样本。

新的撞击用例必须覆盖两个方向：减速冲边**不**回绕，匀速快推**要**回绕，且都要
在 1000Hz–125Hz 的报点率下成立。

## 调试的边界

任何会移动真实光标、或安装真实全局钩子的排查手段（包括直接启动
`cursorwrap.exe` 做探针），**必须先征得用户同意**。编译、单测、协议往返、
静态校验、以及下面这个 dev shell 都不需要征求。

### 先用 `cw_devshell`，再用真机

验证 GUI 与判定逻辑时**优先**用它，它不移动真实光标：

```powershell
cmake --build build --target cw_devshell
python tests/devshell_scenarios.py                 # 全部场景
build\cw_devshell.exe gui                          # 真窗口，键盘 1/2/3
build\cw_devshell.exe run tests\scenarios\<x>.txt   # 单场景
```

它链接的是插件发布的同一批源文件，所以**壳子里错的就是线上的错**。发现新缺陷时
把复现写成 `tests/scenarios/*.txt` 场景（必须 `monitor` 声明布局 + 至少一条
`expect`），而不是只留一次性的调试代码。

shell 覆盖不到的只有三件事，这些才需要真机与用户同意：真实钩子的安装/重连、
`SetCursorPos` 失败路径、以及**手感**。

验证证据按工作台规范（`winshun-dev/DEVELOPMENT.md`）：仓库内只放精选证据与
机器可读结果（≤5MB），原始素材不进本仓库；验证 README 必含"没测的"一节。
