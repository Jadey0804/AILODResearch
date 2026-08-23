# AILOD MVP Phase 7B 检查点：固定布局与无画面空间查询

**日期：2026-08-23**<br>
**分支：`phase-7-visual-demo`**<br>
**本阶段开始 HEAD：`4f642a9`（Phase 7A 无画面互动连接层封板）**<br>
**当前状态：Phase 7B 实现与验证完成，改动尚未提交，等待项目作者确认；未获确认不得进入 Phase 7C。**<br>
**模型版本：`[FROZEN] v1.9`**<br>
**Demo 协议：v2.0**<br>
**布局版本：`phase7b-layout-v1`**<br>
**领域摘要版本：`1.9-domain-v1`**

## 1. 先用大白话说明这一步解决了什么

这一步仍然没有打开地图，也没有生成房子或 NPC Actor。它先在后台建立了一份确定的“王国空间目录”。

目录里写清楚：王国有多少街区、道路在哪里、哪些位置可以展示住宅、林场/木材采购点/市场在哪里、每个真实 ResidentID 属于哪个街区和道路，以及每个逻辑 HomeID 对应哪个可见住宅槽位。

镜头查询不再翻遍 20k 或 100k 人口。它先算出镜头视锥覆盖哪些空间格子，只查看这些格子里的居民。普通镜头、望远镜、滞回、一个追踪居民和最多 50 人的 Active 请求都在这个空间目录上工作。

PCG、NPC 和地图以后必须共同读取这份目录。房屋 Mesh 是否当前加载，不会反过来决定房屋、道路或 ResidentID 是否存在。

## 2. 本阶段实际完成了什么

### 2.1 固定 Visual World Layout

布局由以下信息共同决定：

- 模拟 Seed、人口规模和人口配置 Hash；
- 独立布局版本 `phase7b-layout-v1`；
- 独立固定 Layout Seed；
- 街区大小、格子大小、每街区人口和住宅展示槽位数量。

相同人口清单、布局版本、配置和 Seed 会得到相同的：

- 街区、道路、住宅槽位和工作点；
- ResidentID、HomeID、VisualHomeSlotID、WorkAnchorID 和道路对应；
- 低层代理固定候选位置；
- Presentation-only 布局 Digest。

布局 Digest 与 v1.9 的 `1.9-domain-v1` 是两回事。它只用于检查展示空间是否重复生成一致，不进入正式领域摘要。

### 2.2 街区、道路、住宅和工作点

每个街区当前生成：

- 4 段十字网格道路；
- 64 个沿道路两侧分布、面朝道路的 `VisualHomeSlot`；
- 1 个林场、1 个木材采购点、1 个市场；
- 每个工作点和居民代理位置都绑定一个布局道路 ID。

每个逻辑 HomeID 有一条只读 `HomeID -> VisualHomeSlotID` 映射。多个 HomeID 可以共用一个展示住宅槽位，因此 20k 居民不需要 20k 栋建筑。

### 2.3 人口增加时扩地图，不把同一格塞满

当前工程默认每 2500 名居民增加一个街区：

| 总人口 | 街区 | 道路段 | 住宅展示槽位 | 工作点 |
|---:|---:|---:|---:|---:|
| 20k | 8 | 32 | 512 | 24 |
| 100k | 40 | 160 | 2560 | 120 |

这不是“一人一栋房”，也不是生成 100k 个 Actor。100k 只是把可查询的王国空间扩展成更多街区，使局部人口密度大致稳定。

### 2.4 空间格子查询

- 初始化布局时允许一次性遍历人口并建立 ResidentID 空间目录和格子索引；
- 镜头运行时只根据视锥的包围范围访问相交格子；
- 格子内再用距离和朝向做精确筛选；
- 普通镜头优先距离，望远镜优先准星中心；
- 查询返回访问格子数、访问居民条目数、匹配数、返回数和是否截断；
- `bScannedResidentCatalog` 在运行时查询中保持 false。

### 2.5 普通镜头、代理和滞回

当前可调软预算：

- 普通镜头最多返回 128 个真实 ResidentID 代理候选；
- 普通镜头最多请求 35 名 Active；
- 新候选使用进入距离；
- 已选择候选允许保留到进入距离的 1.2 倍；
- 同优先级最终按稳定 ResidentID 排序；
- 新旧完整 Active 请求相同且追踪目标未变时，报告“不需要重新提交”。

代理候选只承诺“这个真实居民可以显示在这里”，不承诺他正在执行某个精确领域动作。精确动作仍只能在成功 Lift 后从 Phase 7A 只读快照读取。

### 2.6 望远镜和追踪

- 望远镜使用独立、更远、更窄的格子查询；
- 准星中心 ResidentID 由中心对齐度、距离和稳定 ID 决定；
- 只有当前中心候选可以形成提升计划；
- 提升计划最多包含中心 1 人和 4 名上下文居民；
- 第一版仍只保存一个追踪 ResidentID；
- 追踪居民离开镜头后仍进入 Phase 7A Active 请求，并保留同一个布局位置与身份。

“持续观察多久才允许提升”尚未实现，仍属于 Phase 7E。Phase 7B 只证明中心选择、候选计划和 Phase 7A 会话接线成立。

### 2.7 与 Phase 7A 会话接通

空间规划器输出的仍是 Phase 7A 的 `FUnifiedDemoObservationRequest`：

- 完整目标 Active ResidentID 集合；
- 一个追踪 ResidentID；
- 全局硬上限继续为 50。

自动检查已经把望远镜中心计划提交给真实 Proposed v1.9 Demo 会话，并从只读快照读回相同 Active 数与追踪 ResidentID。

## 3. 当前工程默认值

这些数值是 Phase 7B 无画面工程配置，不是 `[FROZEN]` 模型规则，也不是 Phase 7C 灰盒后的最终地图比例：

| 参数 | 当前值 | 大白话含义 |
|---|---:|---|
| `ResidentsPerDistrict` | 2500 | 人口增加到一定数量后扩一个街区 |
| `HomeSlotsPerDistrict` | 64 | 每街区可供 PCG/地图使用的住宅展示点 |
| `DistrictSize` | 100000 cm | 当前一个街区边长约 1 km |
| `KingdomGap` | 200000 cm | 两个王国展示区域之间约 2 km |
| `SpatialCellSize` | 5000 cm | 查询格子边长约 50 m |
| `NormalProxyBudget` | 128 | 普通镜头最多返回的轻量代理候选 |
| `NormalActiveBudget` | 35 | 普通镜头建议的完整 Active 名额 |
| `TelescopeProxyBudget` | 32 | 望远镜最多保留的代理候选 |
| `TelescopeActiveBudget` | 5 | 望远镜中心和上下文 Active 名额 |
| `ActiveHardCap` | 50 | v1.9 冻结硬上限，不能调整 |
| `ExitDistanceMultiplier` | 1.2 | 约 20% 退出滞回 |

绝对距离、街区视觉大小、道路宽度和建筑密度必须在 Phase 7C 灰盒中看实际效果后再调整并记录。

## 4. 本阶段明确没有做什么

- 没有创建 `.umap`、`.uasset`、World Partition、HLOD、NavMesh 或 PCG Graph；
- 没有生成、加载或卸载任何房屋、树木或工作点 Mesh；
- 没有接 Dear ImGui 或任何 UI；
- 没有点击移动、WASD 相机、F1、暂停或倍率 Controller；
- 没有创建 Cohort 代理 Actor、NPC Character、Actor 池、动画或行为树；
- 没有运行时逐居民移动、路径规划或离屏路线更新；
- 固定 `ProxyPosition` 只是空间候选锚点，不是居民永久厘米级真实位置；
- 没有望远镜持续观察计时、World Partition 临时 Streaming Source 或“正在对焦”；
- 没有把布局保存成 UE Data Asset 或 PCG 输入资产；当前由版本化配置确定性构建；
- 没有 20k/100k 有画面帧率、Draw Call、碰撞、导航、流式加载或 HLOD 测量；
- 没有修改正式 Run Manifest、v1.9 模型规则或领域摘要；
- 没有 commit，也没有 push。

## 5. 修改的文件和公开接口

### 5.1 Source

- `Source/AILODResearch/Presentation/AILODVisualWorldLayout.h/.cpp`
  - 固定布局数据契约、确定性构建、道路/住宅/工作点、HomeID/ResidentID 映射、格子索引、视锥查询和布局 Digest；
- `Source/AILODResearch/Presentation/AILODVisualObservationPlanner.h/.cpp`
  - 普通镜头、望远镜、候选预算、20% 滞回、一个追踪居民和 Phase 7A Active 请求；
- `Source/AILODResearch/Tests/AILODPhase7BTests.cpp`
  - 五项 Phase 7B 自动检查。

### 5.2 主要只读入口

```cpp
bool FVisualWorldLayout::Build(
    const FInitialPopulationManifest& Population,
    const FVisualWorldLayoutConfig& Config,
    FString& OutError);

bool FVisualWorldLayout::QueryCone(
    const FVisualConeQuery& Query,
    TArray<FVisualSpatialCandidate>& OutCandidates,
    FVisualSpatialQueryDiagnostics& OutDiagnostics,
    FString& OutError) const;

bool FVisualObservationPlanner::PlanFrame(
    const FVisualObservationFrameInput& Input,
    FVisualObservationPlan& OutPlan,
    FString& OutError);
```

地图和 PCG 以后读取 District、Road、VisualHomeSlot 和 WorkAnchor；NPC 展示读取 ResidentPlacement 和查询结果。没有任何接口允许它们写 v1.9 金币、住房、任务、事件、Ledger 或 Joint State。

## 6. 自动检查和原始证据

### 6.1 Development Editor 编译

- UE 5.4 Development Editor 编译成功；
- 新增 Presentation 源文件和 Phase 7B 测试均完成编译与链接。

### 6.2 Phase 7B 专项

- `AILODResearch.Phase7B`：5/5 Success、0 Fail、退出码 0；
- 原始日志：`Saved/Logs/AILODResearch-backup-2026.08.23-03.32.28.log`；
- 五项分别为：
  - `FixedLayoutContract`；
  - `ObservationCommitCost`；
  - `ScaleGridBoundary`；
  - `SpatialHysteresisAndTracking`；
  - `TelescopeSessionBridge`。

### 6.3 最终完整回归

- `AILODResearch`：53/53 Success、0 Fail、退出码 0；
- 原始日志：`Saved/Logs/AILODResearch.log`，测试完成时间 2026-08-23 03:40:40（UE 日志时间）；
- 固定轨迹 Digest 仍为 `EC735B18390C50437E52BF77B5C79D3BDB3D1903`；
- 连续性 Digest 仍为 `E90151525DC4525270BC22091D6ED0BC5E96CE00`；
- 固定轨迹样本仍为 `max_active=20`；
- Phase 6H `FormalProvenance` Success；
- Phase 6I 三项住房连续性检查 Success；
- Phase 7A 四项互动边界检查 Success；
- Phase 7B 五项空间检查 Success。

### 6.4 最终工程测量

以下都是当前机器、Development Editor、NullRHI、自动测试夹具的单次工程诊断，不是有画面帧率或论文正式性能结果：

| 项目 | 2k/20k/100k 结果 |
|---|---|
| 2k Demo 初始化 | 约 1.178 ms |
| 2k 首次 35 人原子提交 | 约 0.723 ms |
| 2k 相同集合重复请求 | 约 0.004 ms |
| 20k Demo 初始化 | 约 12.324 ms |
| 20k 首次 35 人原子提交 | 约 4.284 ms |
| 20k 相同集合重复请求 | 约 0.005 ms |
| 20k 布局构建 | 约 3.021 ms |
| 100k 布局构建 | 约 15.341 ms |
| 20k 本地视锥查询 | 约 0.094 ms，访问 425 个居民条目、35 个格子 |
| 100k 本地视锥查询 | 约 0.048 ms，访问 391 个居民条目、35 个格子 |

日志中的 Epic 遥测联网失败、Rider 端口文件删除失败和 Motion Controls 警告是运行环境噪声；自动化控制器没有把它们判为测试失败。

## 7. 每项证据能证明什么、不能证明什么

| 证据 | 能证明 | 不能证明 |
|---|---|---|
| `FixedLayoutContract` | 同一 Seed/版本可重建相同空间；20k 使用共享住宅槽位；HomeID/ResidentID/道路/工作点能解析 | 不能证明 PCG 已生成 Mesh 或道路实际好看 |
| `SpatialHysteresisAndTracking` | 20% 退出带、稳定选择、一个离屏追踪居民和清除追踪有效 | 不能证明 Actor 移动、动画或离屏路线进度 |
| `TelescopeSessionBridge` | 远距中心候选、最多 5 人计划和 Phase 7A v1.9 会话提交接通 | 不能证明持续观察阈值、Streaming Source 或远景 HLOD |
| `ScaleGridBoundary` | 20k/100k 查询走局部格子，访问条目远少于人口总数；世界随人口扩街区 | 不能证明有画面 100k 流式加载或帧率 |
| `ObservationCommitCost` | 当前 2k/20k 下 35 人原子提交有真实工程测量，重复集合几乎不复制 | 不是正式性能门，也不能保证地图/Actor 接入后仍是相同耗时 |
| 53/53 与固定 Digest | 现有 v1.9 正式轨迹、住房连续性、领域摘要和 Phase 7A 边界未被破坏 | 不能代替正式 480/90 Runs，也不能证明 Phase 7C 编辑器资产正确 |

## 8. 对抗性审查后的已知问题

### 8.1 当前空间位置是展示锚点，不是移动模拟

居民代理位置沿固定道路确定性分布，但不会按小时走路。这样可以先完成局部查询，不会为 20k/100k 人运行路径。Phase 7D/E 才能为少量可见/追踪居民保存路线和进度；它仍不能覆盖领域任务。

### 8.2 布局还没有保存成 UE 资产

当前布局由配置和人口清单确定性构建，并有独立 Digest。Phase 7C 必须决定怎样把同一份 District/Road/HomeSlot/WorkAnchor 数据交给 UE 地图和 PCG，不能让 PCG 自己另生成一套无法核对的位置。

### 8.3 地图比例尚未经过肉眼灰盒

1 km 街区、50 m 格子、2 km 王国间距只是无画面默认。Phase 7C 必须在 UE 中查看玩家移动、相机高度、望远镜距离和建筑密度后调整；调整只改 Demo 配置和布局版本，不改 v1.9。

### 8.4 望远镜还没有观察时间门

Phase 7B 只允许当前准星中心形成提升计划，但没有计算“观察多久”。Phase 7E Controller 必须在持续观察达到阈值并确认远处格子加载后才提交提升和追踪。

### 8.5 查询结果有明确截断预算

普通查询最多预取 512 个局部候选，望远镜最多预取 256 个，并报告是否截断。Phase 7C 灰盒如果经常截断，应先调整格子、距离和预算，不能回退到全人口扫描。

## 9. Codex 下一步要做什么

只有作者确认本检查点后，Codex 才提交 Phase 7B 并进入 Phase 7C。Phase 7C 将第一次涉及 UE 编辑器和作者协作，最小顺序是：

1. 先联网核对并固定 UE5.4 Dear ImGui 适配仓库的具体提交、许可证和依赖，不直接把原始 `ocornut/imgui` 当 UE 插件；
2. 准备独立 Demo Controller，负责预热、暂停、1x/2x/4x、每帧最多一个 StepHour、积压和重开；
3. 给作者一份逐步 UE 操作说明，再创建独立 World Partition Demo 地图；
4. 把本阶段同一份道路、住宅槽位和工作点数据接给地图/PCG，而不是重新随机摆一套；
5. 作者按说明选择/放入简单 Mesh，PCG 在编辑器中固定生成并保存房屋和树木；
6. 接 World Partition、HLOD、NavMesh、左键移动、WASD 自由相机和 F1；
7. 接一个功能型 Dear ImGui 窗口，只读 Phase 7A/7B 快照和诊断；
8. 编译、PIE、Development 打包和人工验收；
9. 生成 `Docs/AILOD_MVP_Phase7C_Checkpoint_CN.md` 并再次等待作者确认。

Phase 7C 不得顺便实现 Phase 7D 的 50 个完整 NPC Actor，也不得实现 Phase 7E 的望远镜持续观察提升。

## 10. 项目作者下一步要做什么

现在仍不需要打开 UE 编辑器，也不需要摆放 Mesh。请先检查并确认：

1. 是否接受 20k 默认 8 个街区、512 个共享住宅展示槽位，100k 压力时扩成 40 个街区、2560 个槽位；
2. 是否接受当前 1 km 街区、50 m 格子只是 Phase 7C 灰盒前的可调默认；
3. 是否接受住宅槽位、道路、工作点、ResidentID 和 HomeID 必须来自同一固定布局；
4. 是否接受当前固定代理位置只用于候选查询，不冒充精确个人移动；
5. 是否接受当前 20k 原子提交测量不需要改 v1.9 事务核心；
6. 是否允许进入 Phase 7C，并在 Codex 给出逐步说明后开始配合操作 UE 地图、PCG 和 Mesh。

如果同意，请明确回复“确认 Phase 7B，提交并进入 Phase 7C”。在收到这句话前，Codex 不提交本阶段、不安装插件、不下载第三方代码，也不要求作者操作 UE。

## 11. v1.9 和 Git 保护结论

- `[FROZEN] v1.9` 模型规则：未修改；
- 模型版本：仍为 `1.9`；
- 领域摘要版本：仍为 `1.9-domain-v1`；
- 正式固定轨迹：未修改，固定 Digest 未变；
- UI/Actor 第二权威：本阶段没有 UI/Actor，也没有领域写接口；
- 每帧全人口扫描：布局初始化可一次性 `O(N)`；视锥查询只访问相交格子，20k/100k 诊断均未扫描人口目录；
- 完整 NPC Actor：未创建；低层候选全部绑定真实 ResidentID；
- PCG 反向权威：没有 PCG；文档明确下一步 PCG 必须读取本布局；
- Demo 混入正式实验：未修改正式 Runner 或实验日志；Phase 7A 双重拒绝仍通过；
- Git：Phase 7B 改动尚未提交；
- push：未执行，也未获得授权；
- 下一阶段：等待作者确认，未获确认不得进入 Phase 7C。
