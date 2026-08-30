# AILOD MVP Phase 7E 检查点：远距观察、Lift 与同一居民追踪

**日期：2026-08-30**<br>
**分支：`phase-7-visual-demo`**<br>
**阶段基线：`4209962`（Phase 7D 已封板提交）**<br>
**当前状态：代码完成、Editor Development 编译通过、Phase 7E 专项 1/1 通过；项目作者已在主地图完成 PIE 望远镜与远距 Lift 验收，本阶段随本次提交本地封板，未 push。**<br>
**模型仍为：`[FROZEN] v1.9`；Demo 协议仍为 v2.0。**

## 1. 大白话说明

Phase 7E 现在形成了一个最小闭环：

1. 按住鼠标右键，镜头立即从俯视切到约 1.7 m 高的近地视角，并收窄视野、显示屏幕中心准星；松开后完整恢复原俯视镜头；
2. 系统只在相机朝向前方 300—1500 m 的窄范围里查找真实居民，不扫描全部 20k 人；
3. UI 显示中心 `ResidentID`、观察时间和远处地图格是否加载完成；
4. 必须持续盯住同一人 1.5 秒，而且远处格子已经 Ready，才提交 Lift；
5. Lift 成功后，这个真实 `ResidentID` 成为唯一追踪居民，并绑定完整 NPC Actor；
6. 松开右键会关闭临时 World Partition Streaming Source，但不会删除追踪身份；
7. 后来再靠近或重新观察，仍然是同一个 `ResidentID`，不是新造一个长得相似的假人；
8. UI 的 `Clear tracked resident` 才会显式结束追踪并允许 Restrict。

这一步没有创建第二套 NPC 数据，也没有让 UI、Actor 或 Streaming Source 直接写模拟状态。最终 Lift/清除仍经过 Phase 7A 已有的权威观察请求、事务和命令日志。

## 2. 实现边界

- 远距候选继续使用 Phase 7B 的固定布局和空间格子索引；
- 望远镜有明确最小距离，避免把玩家身边原本已 Active 的居民冒充为“远距 Lift”；
- 同一中心目标的真实观察时间单独累计；切换中心、关闭望远镜或无目标时清零；
- 加载未完成时可以继续累计观察时间，但绝不提前 Lift；只有“时间达到＋Streaming Ready”同时满足才允许提交；
- 临时 Streaming Source 是运行时创建的小范围球形源，默认半径 300 m、优先级 High；
- 追踪居民始终占用一个 Active 槽，普通镜头、望远镜上下文和追踪总数仍受 50 人硬上限约束；
- 关闭望远镜只撤销临时地图加载源。追踪居民的身份和权威状态继续保留；
- Lift 和清除追踪会进入既有权威命令日志；没有改变权威集合的“关闭望远镜”不会制造重复日志；
- 没有改变金币、木材、住房、动作、8 小时决策、时间倍率或任何 v1.9 领域规则。

## 3. 默认配置

位置：`Edit > Project Settings > AILOD > AILOD Visual Demo > Telescope`。

| 参数 | 默认值 | 用途 |
|---|---:|---|
| `Telescope Observation Distance Meters` | 1500 | 最远查询距离 |
| `Telescope Minimum Distance Meters` | 300 | 排除普通近景居民 |
| `Telescope Observation Half Angle Degrees` | 2 | 屏幕中心窄视锥 |
| `Telescope Focus Seconds` | 1.5 | 同一目标最短持续观察时间 |
| `Telescope Streaming Radius Meters` | 300 | 临时加载源半径 |
| `Telescope Camera Height Meters` | 20 | 当前主地图使用的望远镜镜头高度 |
| `Telescope Camera Pitch Degrees` | -5 | 当前主地图使用的轻微俯角 |
| `Telescope Camera Field Of View Degrees` | 30 | 当前主地图使用的望远镜视野角 |

这些值可以在停止 PIE 后直接修改，下一次 PIE 会生效；不需要重新 Generate PCG、Build Paths、Build HLOD 或重新编译。建议先调镜头高度；只有确实需要向上或向下看时才改 Pitch，因为远距居民查询本身按水平面计算。

## 4. 主要修改

- `Presentation/AILODVisualObservationPlanner.h/.cpp`：最小远距过滤、持续观察门、追踪目标和最多四名望远镜上下文；
- `Presentation/AILODVisualDemoRuntime.h/.cpp`：把中心 Lift、替换追踪和清除追踪原子接入权威会话，并公开只读命令日志副本；
- `Visual/AILODVisualDemoCharacter.h/.cpp`：直接监听鼠标右键；按住时切换近地望远镜视角，松开时恢复原镜头；
- `Visual/AILODVisualDemoWorldSubsystem.h/.cpp`：准星、UI 状态、临时 Streaming Source、Lift 提交和清除追踪；
- `Visual/AILODVisualDemoSettings.h`：五个望远镜参数；
- `Tests/AILODPhase7ETests.cpp`：一条端到端专项检查。

## 5. 当前证据与限制

- `AILODResearchEditor Win64 Development`：近地镜头修正后重新编译成功；UHT `-WarningsAsErrors`、编译和链接均通过；
- `AILODResearch.Phase7E.TelescopeLiftTrackReplay`：1/1 Success，退出码 0；
- 专项检查覆盖：远距中心是真实居民、最小距离生效、加载前不能 Lift、时间与加载双门满足后 Lift、Active 不超过 50、关闭望远镜保留同一追踪 ID、显式清除、保存命令并在同一 Seed/布局/时间重放到相同最终状态；
- 本轮没有运行完整 `AILODResearch` 回归、Game Development、Cook/Package 或有画面性能测量；这些不能由 1/1 专项替代，统一留到 Phase 7F；
- NullRHI 专项不能证明右键手感、准星是否好瞄、真实 World Partition 加载提示和 NPC 画面是否自然，所以仍需要下面的一次短 PIE 验收。
- 项目作者已在主地图确认：按住右键可进入望远镜视角，远处居民能够被 Lift；调整 Pitch 后准星与被选中 NPC 的画面位置存在轻微偏差，当前不阻塞阶段验收，留作 Phase 7F 的表现校准项。

## 6. 项目作者只需做的短 PIE 验收

1. 打开 `/Game/Phase7/Maps/L_Phase7_VisualDemo`，点击 `Play > Selected Viewport`；不改任何 UE 配置。
2. 进入 Running 后，用 WASD/Q/E 让俯视镜头来到远处道路附近，按住鼠标右键。
3. 预期镜头立即下降到接近平地的第一人称观察高度、水平朝向与切换前一致，并看到屏幕中心蓝色准星；调试面板即使收起，准星也必须保留。展开面板后，准星瞄到远处居民时，`Telescope candidates` 应大于 0、`Center ResidentID` 为正数，`Focus` 增长，`Distant cell` 从 `Loading...` 变为 `Ready`。
4. 对同一目标保持约 1.5 秒。预期出现 `Telescope Lift committed`，UI 显示相同的 `Tracked ResidentID`，`Active residents` 不超过 50。
5. 松开右键。预期完整恢复切换前的俯视位置、朝向和视野，望远镜变为 Off，但 `Tracked ResidentID` 仍是刚才同一个值；移动镜头离开再回来时，不应换成另一个身份。
6. 点击 `Clear tracked resident`。预期下一帧追踪 ID 消失，Demo 仍为 `Running`，没有 `Failed`。

如果一直显示 `Center ResidentID: 0`，先看 `Telescope candidates`：为 0 表示准星前方 300—1500 m 的窄范围内没有居民，并非射线故障；用 WASD 平移或 Q/E 旋转，把准星对准远处道路上的居民。候选数大于 0 但中心 ID 仍为 0 才属于错误。真正需要停止并截图反馈的情况还有：Ready 后超过 2 秒仍不 Lift、追踪 ID 与中心 ID 不同、Active 超过 50、松开右键后身份立刻丢失，或状态变为 `Failed`。

## 7. 当前门状态

Phase 7E 已由项目作者完成主地图短 PIE 验收并随本次提交本地封板，未 push。下一阶段是 Phase 7F 总回归、有画面性能测量和最终表现验收。
