# AILOD MVP Phase 7D 检查点：真实居民代理、50 槽 NPC Actor 池与只读选择

**日期：2026-08-24；人工验收与提交收尾：2026-08-29**<br>
**分支：`phase-7-visual-demo`**<br>
**本阶段开始 HEAD：`611534a`（Phase 7C 地图、PCG、相机、UI 与 HLOD 封板）**<br>
**当前状态：Phase 7D 源码、第一轮 PIE 连续性修复和暂停状态下同一分钟 revisit 事务键修复均已完成。8 月 24 日版本通过 Editor/Game Development、8/8 专项和 66/66 完整回归；8 月 26 日唯一事务键修复按项目作者要求只完成 Editor Development 编译，项目作者随后在主地图 PIE 确认返回同一地点后 NPC 不再消失，并批准 Phase 7D。本阶段已本地提交并通过 amend 纳入本验收记录，未 push；Phase 7E 尚未开始。**<br>
**模型版本：`[FROZEN] v1.9`**<br>
**Demo 协议：v2.0**<br>
**布局版本：`phase7b-layout-v1`**<br>
**领域摘要版本：`1.9-domain-v1`**

## 1. 先用大白话说明这一步解决了什么

Phase 7C 之前，UI 虽然能列出 35 名 Active 居民，但地图上只有玩家、道路、房屋和树木，没有真正可点击的居民身体。

Phase 7D 现在补上两层居民画面：

- 远处或尚未提升的居民显示为细小灰盒代理。每一个代理都有真实 `ResidentID`，但不会假装知道这个人的精确动作；
- 已经是 Active 的居民显示为“身体圆柱＋头部球体”的完整灰盒 NPC Actor。一个 Actor 只绑定一个真实 Active `ResidentID`；
- 完整 Actor 不会按 20k 人口生成。系统启动时只创建固定 50 个可复用槽位，普通镜头通常只绑定约 35 个；
- 镜头换位置后，旧 Actor 回池、新居民占用空槽。回池只换画面绑定，不删除居民、不重置任务、不改钱、木材或房屋；
- 左键点击居民只做选择查看。点击低层代理不会偷偷把他 Lift 成 Active；
- `Work`、`BuyWood`、`ChopWood` 和维修动作会从固定布局读取工作点、采购点、林场和住宅展示点。NPC 只在固定道路位置附近做短距离灰盒走动，不冒充精确个人轨迹。

这一整层可以概括为：

```text
固定空间格子查询 ─> 真实 ResidentID 候选 ─> 低层 HISM 代理
v1.9 只读 Active 快照 ───────────────> 固定 50 槽 NPC Actor 池
左键命中代理/Actor ─> 类型化选择请求 ─> 只读 UI 详情
```

第一轮 PIE 已确认两层居民、点击详情和基本回池能够工作，同时暴露了四个需要在 7D 内解决的问题：动作每 8 个游戏小时切换时完整 Actor 会跳回固定点；4x 只加快模型时间而没有加快占位动画；镜头移动时低层代理会因整批重建而闪烁；快速换区时权威 Active 集合拒绝一次替换会把整个 Demo 标成 `Failed`。

本轮已分别改成“同一居民保留本地路线进度”“展示动画显式跟随 Pause/1x/2x/4x”“固定 HISM 槽位只增量更新变化项”和“替换拒绝时保留上一份权威完整 Actor，同时让当前镜头的低层代理继续刷新”。这些 Presentation 连接修复没有改变 v1.9 的 8 小时动作、时钟、Active 上限或领域状态。

第二轮 PIE 又确认了一个更底层的原因：Pause 时游戏分钟不推进，玩家在 A → B → A 之间快速 revisit，合法的 Lift/Restrict 资源迁移可能再次生成同一个“居民＋游戏分钟＋资源”事务键，从而被 Ledger 的重复事务保护拒绝。修复保留原有重复保护：第一次仍使用原键；只有该键已经提交且新的合法 LOD 迁移确实需要发生时，才追加 `REVISIT-N` 唯一后缀。该修复只改变 LOD 边界事务的唯一标识，不改变转移数量、资源公式、事件顺序或任何居民状态规则。2026-08-29，项目作者在主地图 PIE 确认返回同一地点后 NPC 不再消失。

## 2. 本阶段明确没有做什么

- 没有修改金币、木材、住房、任务、政策、竞争、事件或时钟规则；
- 没有修改模拟领域公式或时间语义；`Source/AILODResearch/Simulation/` 的改动仅为 Ledger 已提交键只读查询，以及 Lift/Restrict 合法 revisit 的唯一事务键生成；
- 没有给 NPC 增加第二套 AI、行为树或可写 Blueprint；
- 没有生成 20k/100k 个 Actor、Character 或隐藏对象；
- 没有让代理声明尚未 Lift 的精确砍树、维修或购买动作；
- 没有让 NPC 扫描 PCG 房屋 Actor 来判断道路和地点；
- 没有实现望远镜持续观察、远距 Streaming Source、Lift、追踪和后来找到同一居民；这些属于 Phase 7E；
- 没有做最终 Skeletal Mesh、动画蓝图、材质或选中描边；
- 没有收集 Phase 7F 的有画面 FPS、Draw Call、显存、内存或流送尖峰结论；
- 没有 push。

## 3. 当前实现怎样保护 v1.9

### 3.1 一条单向只读链

`FVisualResidentPresentationPlanner` 只接收三份值数据：固定布局、空间观察计划和 `FUnifiedDemoSnapshot`。它输出新的 `FVisualResidentPresentationFrame` 值拷贝。

Actor、HISM 代理和 ImGui 都只读取这份展示帧。它们拿不到可写 Ledger、事件、Joint State、Home Continuity Registry 或 Active 内部对象。

### 3.2 低层代理不冒充 Active

- 代理来自 Phase 7B 的空间格子候选，不扫描全部人口；
- 每个代理先验证 `ResidentID` 能在固定布局中找到；
- 已经绑定完整 Actor 的 `ResidentID` 会从代理列表排除，画面不会同时显示同一个人的两具身体；
- 代理没有 `FUnifiedDemoResidentSnapshot`，UI 明确显示 `exact action unavailable`。
- HISM 在初始化时只分配固定容量的可复用槽位；保留的 ResidentID 保留原槽，只对进入、离开或换位置的槽调用增量更新，不再每帧清空并重建全部代理。

### 3.3 完整 Actor 只绑定真实 Active

- 展示帧要求 `Snapshot.ActiveCount == ActiveResidents.Num()`；
- 每个 Active ID 必须唯一、为正、存在于固定布局，并且 HomeID 必须一致；
- Actor 池容量硬性要求等于 50，51 人请求会在改变槽位前被拒绝；
- 同一 Active 集合即使输入顺序变化，已有居民仍保留原槽位；
- 替换时先在纯值计划中完整计算释放和新绑定，成功后才应用到 Actor。

### 3.4 选择不是提升

点击请求只接受当前画面上真正显示的代理或 Actor。选择低层代理后只保存 Presentation 层的 `SelectedResidentID` 并重建只读展示帧；不会调用 `SubmitDemoObservationRequest`、`LiftResident` 或 `RestrictResident`。

正式的望远镜观察达到阈值后再 Lift，仍留在 Phase 7E。

### 3.5 可见集替换拒绝不是模型失败

镜头快速换区时，新的观察计划先在一份候选副本中完整计算。已提交 Active 计划和当前镜头的代理展示计划分开保存：只有权威 `SubmitDemoObservationRequest` 接受后，Active 历史、已提交观察计划、只读 Snapshot 和完整 Actor 才一起前进；代理候选仍可使用当前镜头的有界空间查询结果。

若权威层拒绝这次原子替换，Runtime 保持 `Running`，继续保留上一份已提交 Active 集合和完整 Actor，并在 UI 显示一条 `Observation` 告警；当前镜头仍显示由真实 ResidentID 构成的低层代理，同一游戏分钟内的同一失败请求不会每帧重复提交。代理刷新只提交代理滞回历史，不提交候选 Active 历史，也不会把候选居民冒充为已 Lift 的完整 Actor。

## 4. 基础地点、路线和占位动画

当前地点映射来自同一份固定 Visual World Layout：

| Active 当前动作 | 展示目标 |
|---|---|
| `Work` | 该居民固定工作点 |
| `BuyWood` | 所在街区的木材采购点 |
| `ChopWood` | 所在街区的林场 |
| `StartRepair` / `ContinueRepair` | 该 HomeID 对应的共享住宅展示槽位 |
| `Routine` | 该居民固定代理道路附近 |
| `Wait` / `None` | 固定代理位置站立 |

为了不制造一套假的精确移动史，完整 Actor 不会从地图一端按厘米模拟走到另一端。允许移动的动作只在居民固定道路位置附近、默认前后各 8 m 的小范围内往返；这只是“正在活动”的占位动画。工作点/住宅目标用于地点和朝向映射，不会写回领域状态。

Actor 被回池、地图格子卸载或重新绑定，都不会改变 ResidentID、HomeID、当前任务或剩余时间。第一版也不承诺离屏回来后保持同一个厘米坐标或动画帧，这符合 v2.0 §6.3 和 §7.3。

同一 ResidentID 仍占用同一 Actor 槽时，动作切换不会重置其本地路线进度。`Routine` 变成站立动作时会停在当时位置，而不是先跳回道路固定点。占位路线和轻微上下浮动统一使用 Presentation 播放率：Pause 为 0，1x/2x/4x 分别按对应倍率推进；该倍率不创建第二套模拟时钟。

## 5. 修改的文件和公开接口

### 5.1 新增文件

- `Source/AILODResearch/Presentation/AILODVisualResidentPresentation.h/.cpp`：只读展示帧、地点/短路线映射和原子 Actor 槽位计划；
- `Source/AILODResearch/Visual/AILODVisualResidentActor.h/.cpp`：可回池的完整灰盒 NPC Actor；
- `Source/AILODResearch/Visual/AILODVisualPopulationPresenter.h/.cpp`：HISM 代理、固定 50 槽 Actor 池和点击命中映射；
- `Source/AILODResearch/Tests/AILODPhase7DTests.cpp`：8 项 Phase 7D 自动检查；
- `Docs/AILOD_MVP_Phase7D_Checkpoint_CN.md`：本检查点。

### 5.2 修改文件

- `Presentation/AILODVisualDemoRuntime.h/.cpp`：构造/复制展示帧，接收只读选择，提供展示播放率，并把已提交 Active 计划与当前代理展示计划分开保存；
- `Presentation/AILODVisualObservationPlanner.h/.cpp`：允许拒绝路径只提交普通/望远镜代理历史，绝不提交 Active 历史；
- `Presentation/AILODVisualWorldLayout.h/.cpp`：按街区和类型只读查找林场、采购点或市场；
- `Visual/AILODVisualDemoSettings.h/.cpp`：增加代理/Actor 展示配置和默认 Engine 灰盒 Mesh；
- `Visual/AILODVisualDemoWorldSubsystem.h/.cpp`：自动创建 Presenter、更新画面、绘制诊断和选中详情；
- `Visual/AILODVisualDemoCharacter.cpp`：左键先判断是否命中真实居民，未命中才移动玩家；
- v2.0、当前规则索引和 Phase 7C 检查点：只更新阶段状态，不修改模型规则。

### 5.3 新的主要公开边界

```cpp
bool FVisualDemoRuntime::CopyPresentationFrame(
    FVisualResidentPresentationFrame& OutFrame) const;

bool FVisualDemoRuntime::RequestSelectedResident(
    FResidentID ResidentID,
    FString& OutError);

void FVisualDemoRuntime::ClearSelectedResident();

double FVisualDemoRuntime::GetPresentationPlaybackRate() const;

bool UAILODVisualDemoWorldSubsystem::HandleResidentClick(
    const FHitResult& Hit);
```

这些接口不返回可写模拟对象。`CopyPresentationFrame` 是值拷贝；选择请求只改 Demo Presentation 选择状态。

## 6. 当前可调 Demo 参数

位置：`Edit > Project Settings > AILOD > AILOD Visual Demo > NPC Presentation`。

| 参数 | 默认 | 含义 |
|---|---:|---|
| `Low Level Proxy Budget` | 128 | 普通镜头最多选择多少真实低层候选；Active 会从代理显示中排除 |
| `Normal Active Actor Budget` | 35 | 普通镜头最多请求多少完整 Active Actor |
| `Placeholder Walk Radius Meters` | 8 | 完整 Actor 在固定道路点附近的占位移动半径 |
| `Placeholder Walk Speed Meters Per Second` | 1.5 | 灰盒走动速度，只是 Presentation |
| `NPC Ground Z Centimeters` | 100 | 当前平地地图的居民脚底高度 |
| `Low Level Proxy Mesh` | Engine Cylinder | 低层代理 Mesh |
| `Full Actor Body Mesh` | Engine Cylinder | 完整 Actor 身体 Mesh |
| `Full Actor Head Mesh` | Engine Sphere | 完整 Actor 头部 Mesh |

Actor 池容量 50 不是可调参数，因为它对应冻结的 Active 硬上限。

替换自定义 Static Mesh 不需要改代码，但 Mesh 必须有可用于 Visibility 查询的简单碰撞，否则左键可能点不中。不要把带领域 AI 的 Character Blueprint 填进这些字段；这里只接受 Static Mesh。

## 7. 自动检查和原始证据

### 7.1 编译

- `AILODResearchEditor Win64 Development`：成功；
- `AILODResearch Win64 Development`：成功；
- UnrealHeaderTool 使用 `-WarningsAsErrors` 通过。

这能证明 UE 5.4 的 Editor/Game C++、反射代码和链接通过；不能证明 Cook/Package 或有画面观感。

### 7.2 Phase 7D 专项

- `AILODResearch.Phase7D`：8/8 Success、0 Fail、退出码 0；
- 原始日志：`Saved/Logs/Phase7D_ReturnVisibility_Final_Automation_20260824.log`；
- 项目：
  - `RealResidentRepresentation`；
  - `ActorPoolAtomicRebinding`；
  - `ProxySelectionIsReadOnly`；
  - `AnchorAndPlaceholderRoute`；
  - `PresentationPlaybackRate`；
  - `ProxyStableSlotsIncremental`；
  - `RejectedObservationKeepsRunning`；
  - `SharedPresentationSettings`。

### 7.3 完整回归

- `AILODResearch`：66/66 Success、0 Fail、退出码 0；
- 原始日志：`Saved/Logs/Phase7D_ReturnVisibility_Final_FullRegression_20260824.log`；
- 固定轨迹 Digest：`EC735B18390C50437E52BF77B5C79D3BDB3D1903`；
- 连续性 Digest：`E90151525DC4525270BC22091D6ED0BC5E96CE00`；
- 固定轨迹 `max_active=20`；
- 2k、10k、20k、50k、100k 工程检查仍为 `identity_scans_per_hour=0`；
- Phase 7A、7B、7C、7D 全部 Success。

这些 NullRHI 自动检查不能证明灰盒 NPC 在屏幕上大小合适、鼠标容易点中或有画面帧率稳定，所以还需要第 10 节的一次短 PIE 验收。

### 7.4 后续唯一事务键修复的验证边界

- 2026-08-26：`AILODResearchEditor Win64 Development` 重新编译成功；生成的 `UnrealEditor-AILODResearch.dll` 时间为 2026-08-26 09:45；
- 2026-08-29：项目作者在正式主地图 PIE 确认 revisit 同一地点时 NPC 不再消失；
- 按项目作者“先做到编译通过，不持续追加测试”的明确要求，8 月 26 日修复后没有重新执行 8/8 专项或 66/66 完整回归；
- 因此第 7.2—7.3 节证据只对应 8 月 24 日的 Presentation 连续性版本，不能冒充最新唯一事务键改动的自动回归证据。该边界必须在 Phase 7F 总验收时通过最新 HEAD 的完整检查重新关闭。

## 8. 每项证据能证明什么、不能证明什么

| 证据 | 能证明 | 不能证明 |
|---|---|---|
| `RealResidentRepresentation` | 代理/Actor 都是真实 ResidentID；完整 Actor 与 Active 一对一；两层不重号；不全员扫描 | 不能证明屏幕上的 Mesh 清晰好看 |
| `ActorPoolAtomicRebinding` | 50 槽、稳定槽位、释放/重绑和 51 人拒绝都是原子的 | 不能证明快速镜头时没有渲染尖峰 |
| `ProxySelectionIsReadOnly` | 选择低层代理不会 Lift、不会改 Active 集合；展示帧是值拷贝 | 不能证明鼠标命中面积舒服 |
| `AnchorAndPlaceholderRoute` | 维修/工作/购买/砍树读取正确固定地点；短路线受配置上限约束；动作切换保持同一条本地路线位置 | 不代表厘米级真实个人轨迹或最终动画 |
| `PresentationPlaybackRate` | Runtime 向展示层提供 0/1/2/4 倍播放率，Pause 为 0 | 不能证明 4x 的有画面观感一定舒服 |
| `ProxyStableSlotsIncremental` | 保留代理保持槽位，离开/进入只改必要槽位，容量溢出不提交 | NullRHI 不能直接证明屏幕完全无闪烁或无渲染尖峰 |
| `RejectedObservationKeepsRunning` | 非空 Active 集合的原子替换被拒绝后仍为 Running；旧 Active Snapshot、已提交计划和完整 Actor 保持不变；暂停 A→B→A 时当前代理按真实 ResidentID 刷新且两层不重号 | 不能证明真实游玩中拒绝出现的频率，也不代表被拒绝居民已经 Lift |
| 双目标编译 | Editor 和 Game 目标可编译链接 | 不等于 Cook/Package |
| 66/66 与固定 Digest | v1.9 正式轨迹、住房连续性和旧阶段回归未变 | 不等于正式实验或有画面性能结论 |

## 9. 对抗性审查后的已知限制

### 9.1 “看到在走”不等于模拟了精确路线

空间查询仍使用 Phase 7B 的固定代理位置。Actor 只在该位置附近短距离移动，避免视觉身体跑出查询区，也避免为 20k 人保存逐帧路径。论文或演示中不能把它说成完整个人寻路模拟。

### 9.2 当前 Mesh 是功能灰盒

细圆柱是低层代理，“圆柱身体＋球形头”是完整 Actor。它们只帮助辨认两种层级和点击选择，不代表最终角色美术、骨骼动画或 LOD 质量。

### 9.3 当前地面高度是一个配置值

当前 Phase 7 平地使用 `NPC Ground Z Centimeters = 100`。以后改成起伏地形时，需要增加有预算的地表投射或分区高度数据；不能每帧为全部人口做地面 Trace。本阶段不提前加入这套复杂度。

### 9.4 望远镜还不能升级代理

Phase 7D 已经证明“真实代理可以被点击且选择不 Lift”。持续观察阈值、中心目标提升、临时地图加载源、追踪以及后来找到同一人必须在 Phase 7E 单独实现和验收。

### 9.5 `deferred` 期间完整 Actor 不会强行换区

`Observation` 告警表示 v1.9 权威层没有接受这次 Active 集合替换。此时当前区域仍应显示真实 ResidentID 的细圆柱代理，但完整 Actor 继续对应上一份已提交 Snapshot；Presentation 不会绕开重复事务或 hard-error 审计来强行 Lift。合法的暂停 revisit 已通过唯一事务键修复，不应再因为同一分钟键碰撞而长期 `deferred`；其他真实拒绝仍按这里的安全降级处理。

## 10. 项目作者只需做的一次简单 UE 验收

### 10.1 不需要配置或创建资产

本轮不需要新建 Blueprint、PCG Graph、Mesh、NavMesh、HLOD 或地图 Actor，也不要重新 Generate PCG、Build Paths 或 Build HLOD。

1. 确认 UE 和 Live Coding 原本已关闭；
2. 双击 `AILODResearch.uproject`，等待 UE 5.4 打开；
3. 确认当前地图是 `/Game/Phase7/Maps/L_Phase7_VisualDemo`；
4. 点击 `Play > Selected Viewport`；
5. 等预热从 Day -7 到 Day 0 结束。

### 10.2 修复后四项验收及结果

1. **8 小时切换不跳位：**先用 1x 看一名正在短距离走动的完整 NPC，等待约 16 个现实秒，跨过至少两次 8 游戏小时边界。NPC 可以停下或继续走，但不应整批瞬移到道路两边再马上跳回来。
2. **Pause/倍速同步：**依次点 1x、2x、4x 和 Pause。短距离走动与轻微上下浮动应随倍率明显变快，Pause 时 NPC 动画完全停住；Pause 后相机仍可移动、旋转和点击居民。
3. **代理不整批闪烁：**持续用 WASD 横移相机经过两批居民约 10 秒。视野边缘少量代理正常出现/消失可以接受，但视野内整批细圆柱不应反复闪空，也不应等停下后才整批冒出来。
4. **换区不再 Failed 或整片空白：**Pause 后移动到另一批居民，再回到先前区域。UI 的 `State` 应始终为 `Running`，`Full NPC Actors` 不超过 50，`Full population scan` 仍为 `No`。若出现 `Observation` 告警，当前区域仍应看见细圆柱代理；完整 Actor 暂时保留上一权威批次是正常降级，但道路不应整片无人。

人工结果：项目作者先后确认 8 小时动作切换不再跳回道路两边、Pause/1x/2x/4x 展示速度正常、碰撞与基本代理显示正常；在唯一事务键修复后，又于 2026-08-29 在主地图 PIE 确认 revisit 同一地点时 NPC 不再消失。该结果构成 Phase 7D 的有画面功能验收，不替代 Phase 7F 的正式帧时间、加载尖峰、内存或显存测量。

本轮只做上述约 30 秒功能检查，不需要记录 FPS，也不需要重新 Build PCG、NavMesh 或 HLOD。

### 10.3 只有悬空或埋地时才改一个值

1. 退出 PIE；
2. 打开 `Edit > Project Settings > AILOD > AILOD Visual Demo`；
3. 展开 `NPC Presentation`；
4. 只微调 `NPC Ground Z Centimeters`；
5. 当前默认是 `100`，每次先改 25—50 cm 后重新 PIE；
6. 不要改 Landscape、PCG Mesh Offset 或 NavMesh 来补偿 NPC 高度。

### 10.4 停止条件

出现以下任一情况就退出 PIE并截图，不要自行绕过：

- UI 显示完整 NPC Actor 超过 50；
- `Full population scan: YES (ERROR)`；
- 点击代理后 Active 数立刻因为“选择”而增加；
- 同一个位置同时出现同一居民的代理和完整 Actor；
- 玩家被 NPC 灰盒碰撞卡住；
- 完整 NPC 每 8 游戏小时明显跳回固定点；
- 4x 下 NPC 占位动画仍与 1x 一样快，或 Pause 后仍在动；
- 视野内代理整批反复闪空、残留幽灵代理，或点击代理读到另一个 ResidentID；
- 状态变为 `Failed`；
- 需要修改金币、住房、任务、PCG 或模拟代码才能继续。

## 11. Codex 下一步与项目作者下一步

Codex 已完成源码、两轮 PIE 问题修复、8 月 24 日双目标编译与 8/8 专项、66/66 完整回归，以及 8 月 26 日唯一事务键修复后的 Editor Development 编译。项目作者已完成第 10.2 节所需的分轮验收并批准 Phase 7D；Phase 7D 已本地封板，未 push。最新改动的完整自动回归按已批准范围留到 Phase 7F 总验收补做。

下一步允许单独开始 Phase 7E 望远镜连续性，但本次收尾不实现 Phase 7E。200 人 Debug 小地图不再是默认下一步，仅在主地图再次出现整批消失、长期 `deferred`、重复身体或 revisit 不连续时，才按备用 Debug 流程启用。

## 12. v1.9 与 Git 保护结论

- `[FROZEN] v1.9` 模型规则：未修改；
- `Source/AILODResearch/Simulation/`：仅增加已提交事务键只读查询和合法 revisit 唯一 LOD 事务键；没有修改领域公式、转移数量、事件顺序或时钟；
- 模型/领域版本：仍为 v1.9 / `1.9-domain-v1`；
- 正式固定轨迹和连续性 Digest：8 月 24 日完整回归未变；8 月 26 日唯一事务键修复后未重新计算，设计上只触及互动 LOD 边界，但仍须在 Phase 7F 用最新 HEAD 复核；
- 正式/互动模式：仍在初始化时互斥；
- UI/Actor/代理第二权威：没有，全部读取值快照；
- 代理假人口：没有，每个代理先验证真实 ResidentID；
- 完整 Actor 第二 AI：没有，短路线只属 Presentation；
- Actor 数：固定池 50，不随 20k/100k 人口增长；
- 每帧全人口扫描：没有，空间查询后只处理有界候选和最多 50 Active；
- Demo 混入正式数据：没有；
- Git：分支 `phase-7-visual-demo`，当前 HEAD 为 Phase 7D 本地封板提交；Phase 7C 基线仍为 `611534a`；
- push：未执行，也未获得授权；
- 下一阶段：项目作者已批准 Phase 7D；Phase 7E 尚未开始，仍需作为独立范围实现、验收和提交。

## 13. 给项目作者的最小回复

Phase 7D 已关闭。开始下一阶段时，项目作者只需回复：

`开始 Phase 7E，按最小望远镜连续性范围实现。`
