# AILOD MVP Phase 7C 检查点：UE 灰盒地图、固定 PCG、相机移动与功能 UI

**日期：2026-08-23；最后更新：2026-08-24**<br>
**分支：`phase-7-visual-demo`**<br>
**本阶段开始 HEAD：`5621303`（Phase 7B 固定布局与空间查询封板）**<br>
**当前状态：Phase 7C 的代码、插件、独立 World Partition 地图、固定 PCG、分区导航、相机/UI、PCG/HLOD 构建与有画面验收均已完成；最终 Editor/Game Development 编译、Phase 7C 5/5 和完整 58/58 回归通过；等待项目作者最终确认后提交，尚未进入 Phase 7D。**<br>
**模型版本：`[FROZEN] v1.9`**<br>
**Demo 协议：v2.0**<br>
**布局版本：`phase7b-layout-v1`**<br>
**领域摘要版本：`1.9-domain-v1`**

## 1. 先用大白话说明现在到了哪里

Phase 7C 的整条灰盒链路已经接通：独立互动 Demo 从 Day -7 预热到 Day 0，按 1x/2x/4x 推进；Dear ImGui 只读复制快照；左键只移动玩家，Q/E 独立旋转镜头，WASD 脱离，F 回到玩家；固定 PCG 从 Phase 7B 的同一布局生成道路、住宅槽位、工作点和树木点；World Partition、分区 NavMesh 和 HLOD 负责运行时近处加载、移动与远景连续。

仓库中现在已有真正的 Phase 7 地图、PCG Graph、HLOD Layer、外部 Actor、导航分块和 HLOD 代理。项目作者已经在 PIE 中验证 UI、首小时持续运行、相机操作、点击移动、PCG 碰撞、卸载 Landscape Region 后的导航，以及远处地面和建筑轮廓不会变空白。

第一次 PIE 暴露出的 Day 0 Active+地震连续性缺口已经做了最小修复，并由专项测试、完整回归和真实 PIE 共同保护。最终回归保持冻结 Digest、Active 上限和零身份全扫描不变。

因此 Phase 7C 的实现与证据已经完成，当前只等待项目作者做一次简单的最终检查点确认。确认前不提交；确认后只提交 Phase 7C，不 push，然后才进入 Phase 7D。

## 2. 当前代码实际完成了什么

### 2.1 独立互动 Demo Controller

- 默认总人口为 20k，即两个王国各 10k；
- 显式启动 `Proposed + v1.9 + StateImport + formal_run=false`；
- Day -7 到 Day 0 在加载状态预热，每帧最多执行一个 `StepHour`；
- Day 0 后进入运行状态，1x/2x/4x 只增加待执行小时，积压留到后续帧；
- 暂停不冻结相机和 UI；
- Day 60 停止并提供重开；
- 镜头观察只通过 Phase 7B 空间规划器形成完整 Active 替换请求；
- UI、Character 和地图没有获得可写领域对象。

### 2.2 地图专用启动门

互动运行时不会在所有地图自动启动。只有当前地图的 GameMode 是 `AILODVisualDemoGameMode`，World Subsystem 才会初始化互动 Demo。

这意味着：

- 原来的 `ThirdPersonMap` 不会因为启用了插件就偷偷运行互动 Demo；
- 正式固定轨迹不会和相机观察同时争抢 Active 名额；
- Phase 7 地图必须显式设置 GameMode 才能看到 UI 和互动时钟。

### 2.3 功能型 Dear ImGui 窗口

窗口当前显示：

- `Interactive Demo (not formal data)`；
- 模型 v1.9、领域摘要 `1.9-domain-v1`、Demo 协议 v2.0；
- 总人口、游戏时间、运行状态；
- 暂停、继续、1x、2x、4x、积压小时和重开；
- 两个王国的住房摘要；
- 最多 10 行 Active 居民只读信息；
- 空间查询访问格子数、居民条目数，以及是否发生全人口扫描。

项目代码明确关闭 Docking 和多视口；没有调用 ImPlot 或 NetImGui。点击 UI 时，输入捕获会阻止同一次点击落到地图移动上。

### 2.4 玩家和相机

- 左键地面：玩家标记通过导航移动；跟随模式下相机随玩家位置移动，但镜头不继承玩家转身；
- Q/E：围绕当前相机支点向左/向右旋转；跟随玩家时不会因此脱离；
- WASD：按当前镜头方向在地面平面自由移动，并使相机脱离玩家；
- 自由相机状态下左键仍可命令玩家移动，相机不会自动弹回；
- F：相机平滑回到玩家并恢复跟随，保留当前 Q/E 朝向；
- 当前玩家只是灰盒圆柱标记，不是 Phase 7D 的 NPC Actor。

左键 NPC 选择尚未完成，因为 Phase 7D 才创建带真实 ResidentID 的代理和完整 NPC Actor。

### 2.5 一份可调配置同时供运行时和 PCG 读取

`Project Settings > AILOD > AILOD Visual Demo` 提供：

| 参数 | 当前默认 | 用途 |
|---|---:|---|
| `PopulationPerKingdom` | 10000 | 两国总人口 20k |
| `SimulationSeed` | 20260810 | 与人口清单一致的模拟 Seed |
| `LayoutVersion` | `phase7b-layout-v1` | 展示布局协议版本 |
| `LayoutSeed` | 20260823 | 固定布局和树木位置 Seed |
| `ResidentsPerDistrict` | 2500 | 每增加多少居民扩一个街区 |
| `HomeSlotsPerDistrict` | 64 | 每街区共享住宅展示槽位 |
| `DistrictSizeMeters` | 500 | 灰盒后收紧为 500 m 街区，减少出生点附近空旷感 |
| `KingdomGapMeters` | 1000 | 灰盒后收紧为两王国之间 1 km 间隔 |
| `SpatialCellSizeMeters` | 50 | 镜头查询格子，不等于 World Partition Cell |
| `NormalObservationDistanceMeters` | 200 | 普通镜头观察距离 |
| `NormalObservationHalfAngleDegrees` | 60 | 普通镜头半视角 |
| `TreesPerDistrict` | 128 | PCG 树点密度 |
| `RoadWidthMeters` | 8 | 灰盒道路宽度 |

这些都是 Demo/灰盒参数，后续可以调整。调整后必须重新启动 Demo，并对 PCG 执行 Cleanup 后重新 Generate；如果最终改变了空间布局含义，还必须提升 `LayoutVersion` 并更新检查点。它们不会改 v1.9 模型规则。

### 2.6 固定 PCG 输出

新增的 `AILOD Visual Layout` PCG 节点不自己发明第二张地图。它读取上表配置，重建与 Phase 7B 相同的人口清单和 `FVisualWorldLayout`，输出四个固定 Pin：

- `Road Segments`：20k 默认 32 段；
- `Home Slots`：20k 默认 512 个；
- `Work Anchors`：20k 默认 24 个；
- `Tree Points`：默认 8 × 128 = 1024 个。

PCG 使用当前分区格子的边界筛选输出。每个格子只拿到本格子的点，不会在每个分区 Actor 里重复生成整张王国。NPC 展示也不会反向扫描 PCG Actor 来判断道路或房屋是否存在。

第一版采用 **Editor 里固定生成并保存**，不使用运行时随机生成整张王国。玩家离开后由 World Partition 卸载已经保存的格子；远处轮廓由 HLOD 负责。望远镜临时 Streaming Source 留到 Phase 7E。

## 3. 第三方 UI 接入记录

- UE 适配仓库：`https://github.com/VesCodes/ImGui`；
- 固定 Adapter commit：`71f29cf675b7d501af4f8e152fb73145f24a3f58`；
- Adapter version：1.3；
- 内含 Dear ImGui 1.92.8，对应 upstream commit `b61e56346a92cfcaf1f43a545ca37b0b32239654`；
- Adapter、Dear ImGui、ImPlot、NetImGui 的许可证文件均保留，均为 MIT；
- 本地来源说明：`Plugins/ImGui/AILOD_THIRD_PARTY.md`；
- 为 UE 5.4 做了一处兼容补丁：在 `ImGuiContext.cpp` 中用 UE 5.4 接受的 `TConstArrayView64<uint8>` 构造替代 upstream 的 `MakeConstArrayView`；
- 其余 upstream 源码未修改；
- 没有下载或启用远程 UI 服务。

原始 `ocornut/imgui` 是 GUI 库，不是完整 UE 插件；本项目实际固定的是 UE Adapter。上游说明和许可证可查：

- `https://github.com/VesCodes/ImGui`；
- `https://github.com/ocornut/imgui`；
- `https://github.com/ocornut/imgui/blob/master/LICENSE.txt`。

## 4. 修改的文件和公开接口

### 4.1 工程和插件

- `AILODResearch.uproject`：启用 `ImGui` 和 Epic `PCG` 插件；
- `Source/AILODResearch/AILODResearch.Build.cs`：增加 ImGui、PCG、导航和输入所需模块依赖；
- `Config/DefaultInput.ini`：增加 WASD、Q/E、左键和 F 的 Phase 7 输入映射；
- `Plugins/ImGui/`：固定 UE Adapter、第三方代码和许可证。

### 4.2 Runtime 和 Visual

- `Source/AILODResearch/Presentation/AILODVisualDemoRuntime.h/.cpp`：预热、时钟、暂停/倍率、空间观察、只读快照和重开 Controller；
- `Source/AILODResearch/Simulation/AILODV17UnifiedRuntime.cpp`：地震结算时把最多 50 名 Active 居民与聚合计数互斥，避免同一居民被重复计算；
- `Source/AILODResearch/Simulation/AILODV17DynamicLOD.cpp`：受灾 Active 居民同步到受损住房状态；若正在执行任务，则保留动作、事件、剩余时间和预约，只同步其连续性快照；
- `Source/AILODResearch/Visual/AILODVisualDemoSettings.h/.cpp`：运行时与 PCG 共用的 Project Settings；
- `Source/AILODResearch/Visual/AILODVisualDemoWorldSubsystem.h/.cpp`：专用 GameMode 启动门、镜头查询和 ImGui；
- `Source/AILODResearch/Visual/AILODVisualDemoGameMode.h/.cpp`：独立 Demo GameMode；
- `Source/AILODResearch/Visual/AILODVisualDemoCharacter.h/.cpp`：左键移动与镜头朝向解耦、Q/E 镜头旋转、WASD 自由相机和 F；
- `Source/AILODResearch/Visual/PCG/AILODVisualLayoutPCGSettings.h/.cpp`：固定布局 PCG 节点和分区格子筛选；
- `Source/AILODResearch/Tests/AILODPhase7CTests.cpp`：5 项 Phase 7C 自动检查，其中两项专门覆盖 Day 0 Active+地震和 Pending Active+地震连续性。

### 4.3 主要公开边界

```cpp
bool FVisualDemoRuntime::Initialize(
    const FVisualDemoRuntimeConfig& Config,
    FString& OutError);

bool FVisualDemoRuntime::Tick(float DeltaSeconds, FString& OutError);
bool FVisualDemoRuntime::SubmitObservationFrame(
    const FVisualObservationFrameInput& Input,
    FString& OutError);
bool FVisualDemoRuntime::CopySnapshot(FUnifiedDemoSnapshot& OutSnapshot) const;

bool UAILODVisualDemoWorldSubsystem::RequestPaused(bool bPaused, FString& OutError);
bool UAILODVisualDemoWorldSubsystem::RequestTimeScale(int32 TimeScale, FString& OutError);
bool UAILODVisualDemoWorldSubsystem::RequestRestart(FString& OutError);
```

不存在“UI 返回可写模拟对象”的接口。UI 请求只能进入 Controller；快照是值拷贝。

## 5. 已完成的自动证据

### 5.1 编译

- Day 0 修复及新版相机调整后，UE 5.4 `AILODResearchEditor Win64 Development`：成功；
- Day 0 修复及新版相机调整后，`AILODResearch Win64 Development` 非 Editor 目标：成功；
- 这证明 C++ 与插件可编译、可链接，不等于已经 Cook/Package，也不证明 UI 在 PIE 中可见。

### 5.2 Phase 7C 专项

- `AILODResearch.Phase7C`：5/5 Success、0 Fail、退出码 0；
- 新版相机调整后专项完成时间：2026-08-23 23:36:44（UE 日志时间；本地为 2026-08-24 00:36:44）；
- 项目分别为：
  - `Day0ActiveEarthquakeContinuity`；
  - `PendingActiveEarthquakeContinuity`；
  - `RuntimeClockBoundary`；
  - `ReadOnlyUISnapshot`；
  - `SharedLayoutSettings`。

### 5.3 最终完整回归

- `AILODResearch`：58/58 Success、0 Fail、退出码 0；
- 原始日志：`Saved/Logs/AILODResearch.log`；
- 新版相机调整后完成时间：2026-08-23 23:45:42（UE 日志时间；本地为 2026-08-24 00:45:42）；
- 固定轨迹 Digest：`EC735B18390C50437E52BF77B5C79D3BDB3D1903`；
- 连续性 Digest：`E90151525DC4525270BC22091D6ED0BC5E96CE00`；
- 固定轨迹样本仍为 `max_active=20`；
- Phase 7A、7B 和 7C 自动检查全部 Success。

这些检查使用 NullRHI。它们不能当作有画面 FPS、Draw Call、流送卡顿或 HLOD 质量证据。

### 5.4 第一次 PIE 人工检查（2026-08-23）

项目作者已完成 10.6 的第一次现场检查：预热、v1.9/`1.9-domain-v1`/20k 标识、`Full population scan: No`、左键移动、WASD 自由相机、自由相机下继续命令玩家、UI 输入抢占，以及按钮出现时的 Pause/1x/2x/4x 操作均符合预期。

本轮发现两个必须如实记录的问题：

1. F1 被 PIE/Editor 视口用作线框显示快捷键，不能稳定交给 Demo。作者已决定把相机归位改为 F；输入配置和当前 v2.0 规格已同步，等待重启编辑器后复测。
2. Day 0 进入 `Running` 后，镜头先激活了 35 名居民；首个游戏小时执行地震时，6 名已 Active 的受灾居民在 Home Continuity Registry 中变成 `DamagedWaiting`，但 Active 状态仍为 `Healthy`，因此 v1.9 硬检查以 `home_continuity=6` 正确停止运行。Pause/Resume 和倍率按钮只在 `Running` 状态显示；按钮随后消失是因为状态已经变成 `Failed`，不是 UI 被遮住。

原有自动检查分别覆盖了“推进首小时”和“Day 0 镜头激活”，但没有覆盖两件事同时发生。这是 Phase 7C 暴露出的测试缺口，不能通过隐藏错误、放宽 v1.9 硬检查或把失败当作 UI 问题绕过。

### 5.5 Day 0 Active+地震最小修复与回归（2026-08-23）

问题不是地震规则错了，而是同一名居民在“聚合 Cell”和“当前 Active 展示状态”之间切换后，地震只更新了住房权威表，没有同步 Active 连续性状态；首小时硬检查因此正确抓到 6 个不一致。

本次只修这条连接：

1. 地震仍按同一固定 Damage List 受灾，不改变受灾人选、住房规则或事件时点；
2. 最多 50 名 Active 居民先组成小集合，聚合受灾人数跳过其中的受灾者，避免一人同时算进聚合和 Active；
3. 受灾 Active 的住房状态同步为 `DamagedWaiting`；
4. 如果该居民正在执行 Pending 任务，保留原动作、Intent、EventID、ArriveID、预约和剩余时间，只更新住房状态、所属 Cell 和事件连续性快照；
5. 新增测试同时覆盖“Day 0 已 Active 后发生地震”和“执行 Pending 任务时受灾、降级、原任务完成”两条路径。

修复后 Phase 7C 为 5/5，完整回归为 58/58。固定轨迹 Digest 仍为 `EC735B18390C50437E52BF77B5C79D3BDB3D1903`，连续性 Digest 仍为 `E90151525DC4525270BC22091D6ED0BC5E96CE00`。20k、50k 和 100k 工程日志仍为 `identity_scans_per_hour=0`。因此这次改动修的是展示连接与权威状态的同步缺口，没有放宽 v1.9 硬门、没有改变正式固定轨迹，也没有加入全人口扫描。

### 5.6 第二次 PIE 与相机操作调整（2026-08-24）

项目作者完成修复后复测：运行到 `D01T07:00` 仍为 `State: Running`，Pause/1x/2x/4x 持续可见，20k、Active 35/50 和 `Full population scan: No` 正常，10.6 其余既有功能没有发现问题。由此确认上一轮按钮消失确实来自已修复的硬错误，不是 ImGui 隐藏。

本轮作者提出更符合俯视角色扮演游戏的相机操作：左键只让玩家移动，玩家转身不能带动镜头旋转；Q/E 独立控制镜头左右旋转。实现保持角色朝移动方向转身，只让相机支点继承玩家位置而不继承玩家朝向；Q/E 旋转支点，WASD 继续按当前镜头方向平移，F 回到玩家并保留当前旋转角。该改动不能直接写金币、住房、任务或时钟等领域事实；镜头方向变化可以通过既有类型化观察请求改变 Active 表示，但仍受 50 上限、原子替换和固定空间索引约束。

源码和输入配置已修改；作者随后完整关闭 UE 和 Live Coding。新版相机通过 Editor Development、非 Editor Development、Phase 7C 5/5 和完整 58/58 回归，两条冻结 Digest 未变。重启 UE 后的专项 PIE 也已通过：左键移动不旋转世界，Q/E 独立旋转，WASD 脱离，F 回到玩家，其余 10.6 功能正常。

### 5.7 固定 PCG、World Partition 与分区导航现场证据（2026-08-24）

- 已创建 `/Game/Phase7/Maps/L_Phase7_VisualDemo`、`PCG_Phase7_Layout_v1` 和分区 PCG Volume；
- 灰盒后将 `DistrictSizeMeters` 从 1000 m 调为 500 m、`KingdomGapMeters` 从 2000 m 调为 1000 m；这只改变 Demo 布局比例，不改变 v1.9；
- PCG Volume 最终约为 `320000 × 120000 × 10000 cm`，PCG Partition Grid 为 `25600 cm`；Cleanup、Generate 和 Save All 后，编辑器和 PIE 都能看到固定道路、住宅/工作点与树木灰盒；
- World Partition MainGrid Cell Size 为 `25200 cm`，Loading Range 为 `76800 cm`；运行时 PCG 道路能随附近格子正确加载；
- Recast NavMesh 已启用 World Partition 导航并生成 4 个 `NavDataChunkActor`。即使编辑器先卸载 Landscape Region，PIE 仍能在 `NavMeshBoundsVolume` 覆盖范围内点击移动，范围外不能移动，符合当前有限导航范围设计；
- PCG 产物碰撞已在 PIE 中复测正常；没有让玩家或 NPC 扫描 PCG Actor 来决定领域事实。

这些证据证明地图连接、固定生成、近处分区加载、碰撞和当前导航范围可用；不能证明最终美术密度、整张王国都可导航，也不能当作 Phase 7F 的渲染性能测量。

### 5.8 HLOD 构建、远景目测与最终自动回归（2026-08-24）

- 构建前完整备份：`Saved/Phase7C_PreHLOD_Backup_20260824`；
- 清理旧 HLOD 后，Setup 一致性检查确认世界包含 56 个 HLOD Actor；最终构建日志为 `Saved/Logs/Phase7C_HLOD_Build_D3D11_20260824.log`；
- 最终构建完成 56/56，0 error、2 个与 Motion Controls 配置有关的既有 warning；其中日志命名显示 8 个 `_0_0` PCG 层单元和 48 个 `_0_1` Landscape 层单元；
- UE 5.4 的 D3D12/SM6 离线 Landscape HLOD 材质烘焙触发已知的 GPU 矩阵精度 ensure。单个 PCG HLOD 在默认路径可成功；单个 Landscape 和最终 56 个完整 HLOD 改用命令行 `-d3d11` 离线构建后全部成功；
- `-d3d11` 只用于本次离线 HLOD Builder，项目运行配置没有改：`DefaultGraphicsRHI=DefaultGraphicsRHI_DX12`，D3D12 目标仍为 SM6；
- 项目作者在 PIE 中脱离玩家相机并压低视角，确认远处地面没有空白、远处房屋轮廓持续可见；近处 UI、移动和地图仍正常；
- 最终复核再次完成 Editor Development 和 Game Development 编译；`AILODResearch.Phase7C` 为 5/5 Success；完整 `AILODResearch` 为 58/58 Success；
- 最终专项日志：`Saved/Logs/Phase7C_Final_Automation_20260824.log`；最终完整回归日志：`Saved/Logs/Phase7C_Final_FullRegression_20260824.log`；
- 固定轨迹 Digest 仍为 `EC735B18390C50437E52BF77B5C79D3BDB3D1903`，连续性 Digest 仍为 `E90151525DC4525270BC22091D6ED0BC5E96CE00`；2k、10k、20k、50k、100k 证据仍为 `identity_scans_per_hour=0`。

远景目测证明当前 HLOD 能避免灰盒地图在远处突然变空白。由于 Instancing HLOD 仍复用灰盒 Cube，它不能证明最终简化质量、Draw Call 收益、帧时间或望远镜加载源；这些留给 Phase 7F 和美术替换后的测量。

## 6. 每项证据能证明什么、不能证明什么

| 证据 | 能证明 | 不能证明 |
|---|---|---|
| Editor + Game Development 编译 | UE 5.4 能编译链接插件和 Phase 7C C++ | 不能证明 Cook/Package 或目标机器运行 |
| `RuntimeClockBoundary` | Day -7→0、暂停、4x 积压、每帧最多一步成立 | 不能证明真实渲染帧率下节奏舒适 |
| `ReadOnlyUISnapshot` | 快照修改不会反写；Active ≤ 50；互动运行不是正式数据 | 不能证明 ImGui 按钮、鼠标捕获和文字实际显示正确 |
| `SharedLayoutSettings` | 默认 20k 得到 8 街区、512 住宅槽位、32 道路和 24 工作点 | 不能证明 PCG Graph 已创建或 Mesh 没有重叠 |
| `Day0ActiveEarthquakeContinuity` | Day 0 已 Active 的固定受灾居民在首小时后仍保持同一 ResidentID/HomeID，住房状态正确且硬检查通过 | 不能代替真实 PIE 镜头下的人工复测 |
| `PendingActiveEarthquakeContinuity` | Pending Active 受灾时不会重置任务；降级和原任务完成后连续性仍成立 | 不证明所有未来事件类型都已接入可视化 |
| 58/58 与固定 Digest | 本阶段修复后仍没有改变冻结 v1.9 的正式固定轨迹结果 | 不能代替正式 480/90 Runs，也不能证明地图性能 |
| PCG 格子边界代码与现场 Generate | 分区执行时按当前格子筛选点；编辑器和 PIE 均看到固定生成结果，没有每格复制整图 | 不能证明最终建筑密度或美术布局已经定稿 |
| 相机/UI PIE 人工检查 | Day 0 修复后可持续运行到 D01T07；控制按钮、20k、空间查询诊断、Q/E、WASD、F 和左键镜头解耦正常 | 不能证明 NPC Actor、望远镜或目标机器打包运行 |
| 分区 NavMesh 与 PCG 碰撞 PIE | 卸载编辑器 Landscape Region 后，玩家仍可在当前 NavMesh Bounds 内点击移动；PCG 产物碰撞正常 | 不能证明整张 6 km 地图都已建导航，也不是最终寻路性能证据 |
| HLOD 56/56 构建与远景目测 | 完整 HLOD 构建成功；远处地面和房屋轮廓在 PIE 中没有变空白 | Instancing 灰盒不能证明最终 LOD 质量、Draw Call、帧时间或望远镜 Streaming Source |

## 7. 当前明确还没有做什么

- Phase 7C 灰盒地图、PCG、分区导航、相机/UI、World Partition 和 HLOD 已完成并通过本阶段功能验收；
- 当前 Mesh 仍是 Cube/Cylinder 灰盒，地图密度、重要地标、道路外观、树种和最终材质没有定稿；
- 当前 NavMesh 只覆盖明确的 `NavMeshBoundsVolume` 范围，不代表整张王国都能行走；
- 没有进行 Phase 7F 的正式有画面帧时间、Draw Call、流送尖峰、显存/内存和 2k/20k/100k 压力测量；
- 没有 Development Cook/Package；
- 没有低层代理、NPC Actor 池、NPC 选择、动画或行为；
- 没有望远镜观察计时、临时 Streaming Source 或远处 NPC 提升；
- `GameDefaultMap` 仍是原 `ThirdPersonMap`，正式游戏默认入口未改；为方便本阶段工作，`EditorStartupMap` 指向独立的 `L_Phase7_VisualDemo`；
- 没有 commit，也没有 push。

## 8. 对抗性审查后的已知风险

### 8.1 PCG 能省手工，但不是模型权威

PCG 只把固定点批量变成 Mesh。调整房屋大小、树密度和 Mesh 没问题；删除某个已生成房子也不能删除逻辑 HomeID。若以后想手工调整重要地标，应改固定布局配置或增加明确的版本化 Override，不能让 NPC 扫描当前场景来推断世界事实。

### 8.2 World Partition Cell、PCG Partition Grid 和 50 m 查询格子不是一回事

- World Partition Cell：决定地图 Actor 何时加载；
- PCG Partition Grid：决定编辑器生成结果拆成多少个 PCG Actor；
- 50 m 查询格子：只帮助镜头寻找 ResidentID。

三者可以使用不同尺寸。不要为了“看起来统一”把它们强行设成同一个数。

### 8.3 当前 PCG 道路是灰盒直线块

Phase 7B 固定布局每街区是四段规则道路。第一轮用 Cube 拉伸足够验证连接，但不好看，也没有路口修边。后续可以让固定布局输出 Spline 或增加展示 Override；不能因此让运行时 NPC 改为读取任意 PCG 随机道路。

### 8.4 HLOD 已通过灰盒连续性检查，但还不是最终性能结论

UE 5.4 HLOD Builder 已生成 56 个代理，项目作者也在 PIE 低视角确认远处地面和房屋不会变空白，因此 Phase 7C 的远景连续性风险已经关闭。剩余风险是当前 Instancing HLOD 与源 Cube 外形相同，不足以证明最终美术简化质量或性能收益；正式帧时间、Draw Call、流送尖峰和望远镜加载仍必须在 Phase 7F/对应后续阶段测量。

### 8.5 当前没有 NPC Actor 是预期结果

PIE 第一轮只会看到玩家圆柱、固定建筑/树木、UI 和 `Active residents` 诊断，不会看到居民模型。Phase 7D 才实现有真实 ResidentID 的代理和完整 NPC Actor；不要为了让画面热闹手摆假 NPC。

## 9. 先认识四个 UE 名词

- **PCG Graph**：一张“批量摆东西的流程图”。本项目输入固定道路/房屋/树点，输出很多 Static Mesh，不需要逐栋手摆。
- **World Partition**：把大地图切成可加载的格子。玩家附近加载完整 Actor，离开后卸载以节省内存。
- **HLOD**：远处用合并或简化版本代替已经卸载的完整建筑/树林，让远景不是突然空掉。
- **NavMesh**：玩家可走区域。按 `P` 看到绿色，左键移动才有可用路线。

Epic 的 UE 5.4 参考：

- World Partition：`https://dev.epicgames.com/documentation/en-us/unreal-engine/world-partition-in-unreal-engine`；
- PCG 与 World Partition：`https://dev.epicgames.com/documentation/en-us/unreal-engine/using-pcg-with-world-partition-in-unreal-engine`；
- PCG Generation Modes：`https://dev.epicgames.com/documentation/en-us/unreal-engine/using-pcg-generation-modes-in-unreal-engine`；
- World Partition HLOD：`https://dev.epicgames.com/documentation/unreal-engine/world-partition---hierarchical-level-of-detail-in-unreal-engine?lang=en-US`。

## 10. 已完成的第一轮 UE 部署步骤（保留作复现说明）

第 10 节记录项目作者本轮实际采用的部署路径，Phase 7C 已经执行完成。除非以后需要重建地图，否则现在不必重新照做，也不要无故再次 Cleanup PCG、Build Paths 或 Build HLODs。

### 10.0 停止条件

以下任一情况出现就停止，不要自己绕过去：

- UE 提示模块无法编译或插件加载失败；
- `AILOD Visual Layout` 节点搜不到；
- PCG 每个格子都重复出现整套 512 栋房；
- PIE UI 显示的模型不是 v1.9、人口不是 20000，或显示 `Full population scan: YES (ERROR)`；
- 原 `ThirdPersonMap` 也自动出现互动 UI；
- Active 超过 50；
- 操作需要改金币、木材、HomeState 或任何 v1.9 领域字段。

记录报错文字或截图，退出 PIE，发给 Codex。

### 10.1 打开工程并检查插件

1. 双击仓库根目录的 `AILODResearch.uproject`，使用 UE 5.4 打开。
2. 如果提示工程模块需要重建，选择 `Yes`；等待编辑器完整打开。
3. 打开 `Edit > Plugins`。
4. 搜索 `ImGui`，确认项目插件 `ImGui` 为 Enabled。
5. 搜索 `PCG`，确认 Epic 的 `Procedural Content Generation Framework` 为 Enabled。
6. 如果刚勾选插件并要求重启，保存后重启编辑器；不要切换 UE 版本。

**预期：**工程正常打开，Output Log 没有红色模块加载错误。

### 10.2 检查共用配置

1. 打开 `Edit > Project Settings`。
2. 左侧搜索 `AILOD Visual Demo`，或展开 `AILOD`。
3. 先不要改值，确认能看到第 2.5 节列出的参数。
4. 截一张完整设置页截图。

**预期：**`Population Per Kingdom = 10000`，总 Demo 人口因此为 20000。

### 10.3 新建独立 Open World 地图

1. 选择 `File > New Level`。
2. 选择 `Open World` 模板并创建。该模板默认启用 World Partition；不要选现有 `ThirdPersonMap`。
3. 立刻选择 `File > Save Current Level As`。
4. 新建文件夹 `/Game/Phase7/Maps`。
5. 地图命名为 `L_Phase7_VisualDemo` 并保存。
6. 打开 `Window > World Settings`。
7. 在 `GameMode Override` 选择 `AILODVisualDemoGameMode`。
8. 先不要修改项目的 `Game Default Map`。

**预期：**只在这张地图运行 PIE 时才启动互动 Demo。

### 10.4 把默认 2 km 地面换成 6 km × 2 km 平地

默认 20k 布局的两个王国实际占据约 `X=-3000m..-1000m` 和 `X=1000m..3000m`、`Y=-1000m..1000m`。Open World 模板自带的约 2 km 地面正好位于中间空隙，不够用。

只在刚创建的 `L_Phase7_VisualDemo` 中执行：

1. 在 World Outliner 搜索 `Landscape`。
2. 选中这张新地图自带的 Landscape 和其 Streaming Proxy，按 Delete。不要删除灯光、天空或 World Partition 相关 Actor。
3. 切换到 `Landscape` 模式；若快捷键可用，按 `Shift+2`。
4. 进入 `Manage > New`，创建 Flat/空白 Landscape。
5. 设置：
   - Section Size：`63 x 63 Quads`；
   - Sections Per Component：`2 x 2`；
   - Number of Components：`24 x 8`；
   - Scale：`X=200, Y=200, Z=100`；
   - Location：`X=0, Y=0, Z=0`。
6. 点击 `Create`，等待完成后保存。

这会得到约 6.05 km × 2.02 km 的粗平地，足够覆盖第一轮 8 个街区。它不是美术地形，只为验证道路、PCG、导航和流式加载。

**如果你的 UE 面板名称或数值布局不同：**不要猜。截取 `Landscape > New` 面板发给 Codex再继续。

### 10.5 放 PlayerStart 和第一块 NavMesh

1. 回到 `Select` 模式。
2. 打开 `Window > Place Actors`，搜索 `Player Start`，拖入地图。
3. 设置位置约为 `X=-250000, Y=-50000, Z=300`（UE 单位是 cm）。
4. 搜索 `Nav Mesh Bounds Volume`，拖入同一区域。
5. 把 NavMesh Bounds 调到约 1.1 km × 1.1 km，覆盖王国 A 的左下街区；Z 高度只需覆盖地面上下。
6. 按 `P` 切换导航显示。

**预期：**PlayerStart 周围地面出现绿色可导航区域。第一轮先覆盖一个街区，不要一开始为整张 6 km 地图生成巨大动态 NavMesh。

### 10.6 完整重启 UE 后复测 UI、首小时和新版相机

以下是当时用于让 Q/E 输入映射和新组件默认值生效的专项复测步骤；本轮已经全部完成并通过，保留在这里方便以后复现。

1. `Save All`。
2. 选择 `Play > Selected Viewport`。
3. 等待 `Loading: prewarming Day -7 to Day 0...` 结束；60 FPS 下大约 168 帧，不代表 168 秒。
4. 确认 UI 显示：
   - `Interactive Demo (not formal data)`；
   - `Model: v1.9`；
   - `Domain digest: 1.9-domain-v1`；
   - `Population: 20000`；
   - `Full population scan: No`。
5. 预热结束后至少再等 3—5 秒，确认时间推进到 `D00T03:00` 或更晚，状态始终为 `Running`，Pause/Resume 和倍率按钮没有消失，底部没有 `home_continuity` 或 `State: Failed`。
6. 依次测试 `Pause/Resume`、1x、2x、4x。
7. 在玩家周围不同方向各左键一次：玩家应转身并移动，但地面网格/世界的镜头朝向不能跟着转。
8. 按住 Q，确认镜头绕当前支点向左旋转；按住 E，确认向右旋转。若视觉方向相反，停止并反馈，不要自行交换配置。
9. 在跟随模式下用 Q/E 改变角度，再左键移动玩家：相机应继续跟随玩家位置并保留该角度。
10. 按住 W/A/S/D，确认相机按当前镜头方向脱离并自由移动。
11. 自由相机状态下再次左键地面，确认玩家可以移动但相机不自动弹回。
12. 按 F，确认相机回到玩家并恢复跟随，同时保留当前 Q/E 朝向。F1 不再用于 Demo 相机归位。
13. 点击 ImGui 按钮，确认玩家没有因同一次 UI 点击而移动。
14. 截一张 `D00T03:00` 或更晚、同时显示 `State: Running` 和控制按钮的完整 UI 截图，然后退出 PIE。

**注意：**这一轮没有 NPC 是正常的。

### 10.7 创建固定 PCG Graph

1. 在 Content Browser 新建 `/Game/Phase7/PCG`。
2. 在文件夹空白处右键，选择 `PCG > PCG Graph`。
3. 命名为 `PCG_Phase7_Layout_v1`，双击打开。
4. 在 Graph 空白处右键，搜索并放置 `AILOD Visual Layout`。
5. 从 `Road Segments` 接一个 `Static Mesh Spawner`，Mesh 先选 `/Engine/BasicShapes/Cube`。
6. 从 `Home Slots` 接 `Transform Points`，把 Offset Z 设为约 `300 cm`，Scale Min/Max 都先设为约 `X=8, Y=6, Z=6`；再接 `Static Mesh Spawner`，Mesh 选 Cube。
7. 从 `Work Anchors` 接 `Transform Points`，Offset Z 约 `250 cm`，Scale Min/Max 约 `X=10, Y=10, Z=5`；再接 Static Mesh Spawner，Mesh 可先选 Cube 或 Cylinder。
8. 从 `Tree Points` 接 `Transform Points`，Offset Z 约 `500 cm`，Scale Min/Max 约 `X=2, Y=2, Z=10`；再接 Static Mesh Spawner，Mesh 可先选 Cylinder。
9. 将四条 Static Mesh Spawner 输出接入一个 `Merge`，再把 Merge 接到 Graph 的 `Output`。
10. 保存 Graph。

这些 Mesh 和比例只是功能灰盒，之后可换。不要在 Graph 中再加一套随机住宅点或随机道路点。

### 10.8 放置 PCG Volume 并生成

1. 回到地图，`Place Actors` 搜索 `PCG Volume` 并拖入。
2. 命名为 `PCGV_Phase7_Layout`。
3. 位置设为 `X=0, Y=0, Z=0`。
4. 在 PCG Component 的 Graph 字段选择 `PCG_Phase7_Layout_v1`。
5. 调整 Volume 边界，目标覆盖：
   - X：约 `-310000..310000 cm`；
   - Y：约 `-110000..110000 cm`；
   - Z：覆盖地面上下约 10000 cm 即可。
6. 如果 Details 显示 `Brush Settings`，直接改 X/Y/Z 尺寸；如果只显示 Transform Scale，就缩放白色 Volume 框并用视口/测量确认上述边界，不要默认 Scale 数值等于厘米。
7. 在 PCG Component 勾选 `Is Partitioned`。
8. Generation Trigger/Mode 第一轮保持 Editor/Generate On Demand 类模式，不选择运行时动态生成整张地图。
9. 先点击 `Cleanup`，再点击 `Generate`，等待生成结束。
10. `Save All`。

**预期：**两国共出现 8 个规则街区，合计 32 段道路、512 个住宅方块、24 个工作点和 1024 个树木灰盒点。中间约 2 km 是王国间隔。

**关键目测：**同一个街区不应叠着出现许多套完全相同的房屋；每个分区格子不应各自复制整张地图。

### 10.9 第一轮结束时发回什么（已完成）

完成 10.1—10.8 后曾要求先停在 Phase 7C，并发回以下证据。项目作者已经逐项提供并由 Codex 核对：

1. `Project Settings > AILOD Visual Demo` 截图；
2. `PCG_Phase7_Layout_v1` 完整 Graph 截图；
3. 生成后的整张灰盒地图俯视截图；
4. 按 `P` 后 PlayerStart 附近绿色 NavMesh 截图；
5. PIE 中 ImGui 完整截图；
6. 逐项说明左键不转镜头、Q/E、WASD、F、Pause/1x/2x/4x 是否符合预期；
7. World Outliner 中 `PCGPartitionActor` 是否出现、数量大约多少；
8. 任何红色 Output Log、重复建筑、悬空 Mesh 或操作不一致。

这些结果通过后，双方继续在同一个 Phase 7C 内完成了 World Partition、PCG Partition Grid、分区 NavMesh 和 HLOD 构建/远景验证。

## 11. Phase 7C 最终 UE 参数和人工验收

1. World Partition Streaming：启用；MainGrid Cell Size=`25200 cm`，Loading Range=`76800 cm`；
2. PCG Partition Grid Size=`25600 cm`；PCG Volume 约 `320000 × 120000 × 10000 cm`；固定布局使用 500 m 街区和 1000 m 王国间距；
3. 导航：World Partitioned Recast NavMesh，保存 4 个 `NavDataChunkActor`；当前只在 NavMesh Bounds 范围内可移动；
4. HLOD：`L_Phase7_VisualDemo_HLOD0_Instancing`，最终 Builder 生成并构建 56/56；
5. PIE：20k、v1.9、`1.9-domain-v1`、互动非正式标识、Pause/1x/2x/4x、Active 35/50、`Full population scan: No` 均正常；
6. 相机：左键移动不转世界，Q/E 旋转，WASD 脱离，F 回到玩家；
7. 地图：PCG 道路在 PIE 中正确加载，PCG 碰撞正常；卸载 Landscape Region 后，玩家附近地块与导航分块仍能按运行时加载；
8. 远景：低视角下远处地面不空白，房屋轮廓持续可见；
9. 本阶段没有收集可作为论文结论的正式渲染性能数据。正式帧时间、Draw Call、流送尖峰和内存/显存统一留到 Phase 7F；
10. 当前只差项目作者最终确认。未获确认不提交，不进入 Phase 7D。

## 12. Codex 和项目作者下一步要做什么

Codex 已完成最终编译、5/5 专项、58/58 完整回归、HLOD 日志、运行配置、UE 资产和 Git 范围核对，并把结果写入本检查点。项目作者不需要再打开 UE 或阅读长日志，只需检查第 14 节的大白话结论。

若项目作者确认，Codex 下一步只会：

1. 再次核对待提交清单只包含 Phase 7C 范围；
2. 提交 Phase 7C，绝不 push；
3. 输出 Phase 7D 的最小实施计划和双方职责；
4. 等待项目作者明确允许后再开始 Phase 7D，不会一口气实现后续阶段。

## 13. v1.9 和 Git 保护结论

- `[FROZEN] v1.9` 模型规则：未修改；
- 模型版本：仍为 `1.9`；
- 领域摘要版本：仍为 `1.9-domain-v1`；
- 正式固定轨迹：完整回归 Digest 未变；
- 正式/互动互斥：互动 Subsystem 只在专用 GameMode 地图启动；
- UI 第二权威：没有，UI 只读值快照并发送类型化请求；
- PCG 第二权威：没有，PCG 与 NPC 查询共同读取固定布局；
- 每帧全人口扫描：空间诊断为 `No`，20k/50k/100k 工程日志仍为 `identity_scans_per_hour=0`；
- 单帧多步追赶：自动测试证明每帧最多一个 `StepHour`；
- Demo 混入正式实验：互动模式保持 `formal_run=false`；
- 完整 NPC Actor：Phase 7C 未创建；
- Git：分支仍为 `phase-7-visual-demo`，HEAD 仍为 Phase 7B 的 `5621303`；Phase 7C 改动尚未提交；
- push：未执行，也未获得授权；
- 下一阶段：Phase 7C 实现与证据已完成，等待项目作者最终确认；未获确认不得提交或进入 Phase 7D。

## 14. 给项目作者的最小最终检查点

你只需要确认下面四句话符合刚才实际看到的结果，不需要重跑 UE：

1. UI、时间按钮、左键移动、Q/E、WASD 和 F 都能正常使用；
2. PCG 道路/建筑会加载，碰撞和当前 NavMesh 范围内移动正常；
3. 压低视角后，远处地面和房屋没有变空白；
4. 接受当前仍是功能灰盒，NPC Actor、望远镜和正式渲染性能测量留到 7D、7E、7F。

若都同意，回复：`确认 Phase 7C，提交并进入 Phase 7D`。在收到这句话前，Codex 不提交、不 push，也不进入 Phase 7D。
