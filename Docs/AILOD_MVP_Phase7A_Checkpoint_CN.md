# AILOD MVP Phase 7A 检查点：无画面互动连接层

**日期：2026-08-23**<br>
**分支：`phase-7-visual-demo`**<br>
**本阶段开始 HEAD：`a092164`（Phase 7-0 文档封板）**<br>
**当前状态：Phase 7A 实现与验证完成，改动尚未提交，等待项目作者确认；未获确认不得进入 Phase 7B。**<br>
**模型版本：`[FROZEN] v1.9`**<br>
**Demo 协议：v2.0**<br>
**领域摘要版本：`1.9-domain-v1`**

## 1. 先用大白话说明这一步解决了什么

这一步给后台模拟加了一扇受控的“展示窗口”，但还没有画面。

以后地图、望远镜、NPC Actor 和 UI 不需要把手伸进模拟内部。它们只能提出：“我现在想观察这几个真实 ResidentID，并追踪其中一个。”后台先在候选副本上完成整组居民的 Lift/Restrict 和硬错误检查；全部成功后才一次性换掉旧集合。任何一步失败，真实会话保持原样。

后台再把时间、王国摘要和最多 50 名 Active 居民复制成一份独立快照。UI 即使修改自己手里的名字或金币，也只是在改副本，不会改变真正的居民状态。

正式实验和互动 Demo 现在也有两道隔离门：正式固定轨迹不会在 Demo 中运行；正式 Runner 和实验日志写入器都拒绝 Demo 数据。

## 2. 本阶段实际完成了什么

### 2.1 明确的 Demo 模式

- 新增 `EUnifiedRunMode::Demo`；
- 第一版 Demo 只接受 `Proposed + 显式 v1.9 权威运行时 + StateImport`；
- 旧 Proposed 默认运行时或其他场景会在初始化时直接失败；
- 单次会话没有方法或场景切换接口，换法必须重开。

### 2.2 正式轨迹与互动集合互斥

- Validation、Accuracy、Performance 继续执行原 Day 7/8、14/15、30/31、45/46 固定 Activation Trace；
- 只有 Demo 模式跳过该固定轨迹；
- Demo 的 Active 集合只由受控观察请求决定，不会和正式轨迹争抢 50 个名额。

### 2.3 原子观察、释放和一个追踪居民

- 请求内容只有“完整目标 Active ResidentID 集合”和“其中一个追踪 ResidentID”；
- ResidentID 必须为正数、真实存在，并按稳定 ID 去重排序；
- 追踪居民必须包含在目标集合内；
- 全局硬上限继续是 50；
- 空集合表示释放全部展示 Active 居民并清除追踪；
- 整组变化先在权威会话候选副本上完成和审计，全部成功后才一次提交，失败不会留下半套状态。

### 2.4 只读展示快照

快照以值拷贝方式提供：

- Demo 协议、模型、权威模式和领域摘要版本；
- `formal_run = false`、方法、场景、人口和权威游戏时间；
- 两个王国摘要；
- 当前追踪 ResidentID、Active 数和最近一个小时的耗时；
- 最多 50 名 Active 居民的 ResidentID、PersistentID、HomeID、稳定名字与外观 Seed、王国、职业、收入档、金币、维修补助、木材、住房状态、当前动作、事件、剩余时间、Ready 和追踪标记。

快照没有 Ledger、Event Store、Joint Cell、Identity Registry、Home Continuity Registry 或 Active 内部对象的可写指针。

### 2.5 命令记录和回放

- 每次有效或无效观察请求都记录序号、权威游戏时间、规范化请求、是否提交和原因；
- 只允许回放已提交记录；
- 回放必须发生在原来的权威游戏时刻；
- 相同 Seed、时间和命令顺序能得到相同 Active 集合、追踪居民和展示状态。

当前记录保存在会话内存中。独立 Demo 文件目录、空间命令、暂停/倍率和望远镜记录要在后续 Controller/空间阶段接上，不能写进正式 Run Manifest。

### 2.6 Demo 数据隔离

- `FExperimentRunner::RunMatrix` 在创建计划或目录前拒绝 Demo；
- `FUnifiedRunLogWriter::WriteRun` 也拒绝直接传入的 Demo 结果，防止绕过 Runner；
- Demo 快照始终标记 `bFormalRun = false`。

## 3. 本阶段明确没有做什么

- 没有创建地图、World Partition、HLOD、NavMesh、PCG Graph 或空间格子；
- 没有下载或接入 Dear ImGui/UE 适配插件；
- 没有 UI、暂停/倍率控制器、相机、点击移动或 F1；
- 没有 Cohort 低层代理、NPC Character、Actor 池、动画或望远镜；
- 没有把 20k 居民变成 Actor，也没有逐居民路径；
- 没有实现空间候选查询，因此还不能证明镜头查询不会退化；这项由 Phase 7B 验收；
- 没有把命令日志保存到磁盘；当前只证明会话内记录和回放；
- 没有测量 2k/20k 有画面帧率，也没有运行 100k 可视化压力；
- 没有运行 Demo Pilot 或正式实验；
- 没有修改 v1.9 的金币、木材、住房、任务、政策、竞争、Digest 或正式轨迹内容；
- 没有 commit，也没有 push。

## 4. 修改的文件和公开接口

### 4.1 Source

- `Source/AILODResearch/Simulation/AILODUnifiedSimulation.h`
  - 新增 Demo 模式、观察请求/记录、居民快照/总快照和四个会话接口；
- `Source/AILODResearch/Simulation/AILODUnifiedSimulation.cpp`
  - 会话状态校验、显式 v1.9 门和公开接口转发；
- `Source/AILODResearch/Simulation/AILODV17UnifiedRuntime.h/.cpp`
  - Demo 原子集合、追踪、快照、记录、回放和固定轨迹互斥；
- `Source/AILODResearch/Simulation/AILODExperimentRunner.cpp`
  - 正式 Runner 拒绝 Demo；
- `Source/AILODResearch/Simulation/AILODExperimentLogging.cpp`
  - 实验日志写入器拒绝 Demo；
- `Source/AILODResearch/Tests/AILODPhase7ATests.cpp`
  - 新增四项 Phase 7A 自动检查。

### 4.2 最小公开接口

```cpp
bool SubmitDemoObservationRequest(
    const FUnifiedDemoObservationRequest& Request,
    FString& OutError);

bool ReplayDemoObservationRecord(
    const FUnifiedDemoObservationRecord& Record,
    FString& OutError);

bool BuildDemoSnapshot(
    FUnifiedDemoSnapshot& OutSnapshot,
    FString& OutError) const;

bool CopyDemoObservationLog(
    TArray<FUnifiedDemoObservationRecord>& OutRecords,
    FString& OutError) const;
```

没有公开单独写金币、木材、住房、任务或时钟内部状态的接口。

### 4.3 Docs

- 更新 v2.0 状态，不改规则内容；
- 更新权威规则索引到 5.1；
- 把交接文档顶部的“当前 HEAD”改成“Phase 7 基线 HEAD”，避免历史快照被误读；
- 新增本检查点。

## 5. 自动检查和原始证据

### 5.1 改代码前基线

- UE 5.4 Development Editor 编译成功；
- 完整 NullRHI 自动检查：44/44 Success、0 Fail、退出码 0；
- 原始日志：`Saved/Logs/AILODResearch-backup-2026.08.23-02.12.13.log`；
- 固定轨迹 Digest：`EC735B18390C50437E52BF77B5C79D3BDB3D1903`；
- 连续性 Digest：`E90151525DC4525270BC22091D6ED0BC5E96CE00`。

### 5.2 Phase 7A 专项

最终候选副本原子提交版本：

- Development Editor 编译成功；
- `AILODResearch.Phase7A`：4/4 Success、0 Fail、退出码 0；
- 原始日志：`Saved/Logs/AILODResearch-backup-2026.08.23-02.45.35.log`；
- 四项分别是：
  - `AtomicObservationAndSnapshot`；
  - `CommandReplay`；
  - `DemoGuards`；
  - `FixedTraceIsolation`。

### 5.3 最终完整回归

- `AILODResearch`：48/48 Success、0 Fail、退出码 0；
- 原始日志：`Saved/Logs/AILODResearch.log`，测试完成时间 2026-08-23 02:53:36（UE 日志时间）；
- 固定轨迹 Digest 仍为 `EC735B18390C50437E52BF77B5C79D3BDB3D1903`；
- 连续性 Digest 仍为 `E90151525DC4525270BC22091D6ED0BC5E96CE00`；
- 固定轨迹样本 `max_active=20` 未变；
- Phase 6H `FormalProvenance` Success；
- Phase 6I `ActiveBatchRemainder`、`ExactHomeLift`、`RepairedActiveRestrict` 全部 Success；
- 四项 Phase 7A 检查全部 Success。

日志中的 Epic 遥测联网失败、Rider 端口文件删除失败和 Motion Controls 警告是运行环境噪声；自动化控制器没有把它们判为测试失败。

## 6. 每项证据能证明什么、不能证明什么

| 证据 | 能证明 | 不能证明 |
|---|---|---|
| 编译成功 | C++ 接口与 UE5.4 Development Editor 当前可链接 | 不能证明 PIE、打包、地图或 ImGui 可用 |
| `DemoGuards` | 旧 Proposed/错误场景被拒绝；正式 Runner 和实验日志写入器拒绝 Demo | 不能证明以后新增的 Demo 文件格式已经完成 |
| `AtomicObservationAndSnapshot` | ID 校验、50 人上限、失败保持旧集合、释放、追踪和快照副本边界有效 | 不能证明 20k 下高频集合切换的耗时合格 |
| `FixedTraceIsolation` | Demo 穿过 Day 7/8 时正式固定轨迹不激活或释放互动集合 | 不能证明 Phase 7B 镜头选择策略正确 |
| `CommandReplay` | 同时刻按相同命令顺序可重建同一 Active/追踪/居民展示状态 | 不能证明尚未实现的空间、暂停、倍率、相机或望远镜命令可回放 |
| 48/48 完整回归与相同 Digest | 现有 v1.9 自动门、正式轨迹、住房连续性和领域结果未被本阶段破坏 | 不能代替正式 480/90 Runs，也不能证明有画面性能 |

## 7. 对抗性审查后的已知问题

### 7.1 原子集合切换有复制成本

为保证整组“全成功才提交”，当前实现会复制一份 v1.9 权威会话作为候选。底层单人 Lift/Restrict 本身也有事务副本。这不会造成每帧扫描全部人口，因为只有集合真正提交时才运行，而且输入最多 50 个 ID；但如果 Phase 7B 每帧都提交不同集合，20k 下仍可能很贵。

Phase 7B 必须：

- 每帧只查询相交空间格子，不扫描人口表；
- 先在展示层做滞回和稳定排序；
- 新旧目标集合相同则不提交；
- 给集合变化设置明确频率和候选访问计数；
- 在接地图前测量 2k 和 20k 的集合替换耗时；
- 如果证据显示复制成本不可接受，再单独讨论不改变 v1.9 语义的批量事务优化，不能私自改冻结模型。

### 7.2 现在只有后台连接，没有 Demo Controller

暂停、1x/2x/4x、每帧最多一个 StepHour、积压、重开、F1 和输入路由尚未实现。它们不能由未来 UI 直接写模拟内部；应在 Phase 7C 的 Demo Controller 中作为有类型请求实现。

### 7.3 现在还没有空间事实

当前接口只接收 ResidentID，不知道居民在哪里。Phase 7B 才建立固定 Visual World Layout、空间格子、ResidentID 映射、低层代理候选、望远镜查询、滞回和追踪空间连续性。

## 8. Codex 下一步要做什么

只有作者确认本检查点后，Codex 才提交 Phase 7A 并进入 Phase 7B。Phase 7B 只做无画面空间层：

1. 建立独立、版本化的 Visual World Layout 数据契约；
2. 定义 District、VisualHomeSlot、WorkAnchor、道路/路线和空间格子；
3. 用固定 Seed 把真实 ResidentID 稳定映射到空间，不读取 PCG Actor；
4. 实现普通镜头和望远镜只查询相交格子的候选选择；
5. 实现稳定优先级、约 20% 滞回、一个追踪居民和最多 50 人请求；
6. 统计访问格子数、候选数、集合是否变化和提交耗时；
7. 验证 20k、可选 100k 查询不退化为每帧全人口扫描；
8. 生成 `Docs/AILOD_MVP_Phase7B_Checkpoint_CN.md`，再次等待作者确认。

Phase 7B 仍不接 Dear ImGui、不创建 UE 地图、不做 PCG、不生成 NPC Actor。

## 9. 项目作者下一步要做什么

本阶段不需要打开 UE 编辑器，也不需要手工布置地图。现在只需要检查并确认：

1. 是否接受“UI/Actor 只拿值快照，控制只发有类型请求”；
2. 是否接受“完整目标集合＋一个追踪居民”的最小公开接口；
3. 是否接受整组变化先在候选副本完成、全部成功后一次提交；
4. 是否接受 Phase 7B 先做无画面固定布局和空间格子，再到 Phase 7C 给出逐步 UE 地图/PCG 部署说明；
5. 是否接受把原子复制成本列为 Phase 7B 必测项，而不是现在改动 v1.9 内部事务。

如果同意，请明确回复“确认 Phase 7A，提交并进入 Phase 7B”。在收到这句话前，Codex 不提交本阶段、不进入 Phase 7B、不要求作者操作 UE。

## 10. v1.9 和 Git 保护结论

- `[FROZEN] v1.9` 模型规则：未修改；
- 模型版本：仍为 `1.9`；
- 领域摘要版本：仍为 `1.9-domain-v1`；
- 正式固定轨迹：内容未改，只在明确 Demo 模式禁用；
- UI/Actor 第二权威：没有暴露可写领域对象，专项测试证明值副本不能改权威；
- 每帧全人口扫描：Phase 7A 没有新增逐帧人口循环，快照只遍历最多 50 名 Active；
- Demo 混入正式实验：Runner 和实验日志写入器双重拒绝；
- Demo 与正式数据：尚未生成任何 Demo 文件或正式 Run；
- Git：Phase 7A 改动尚未提交；
- push：未执行，也未获得授权；
- 下一阶段：等待作者确认，未获确认不得进入 Phase 7B。
