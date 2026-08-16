# Hierarchical AI / Simulation LOD：MVP 总交接文档（Phase 5 入口）

**交接版本：v1.0**
**日期：2026-08-16**
**目标读者：新的 Codex 会话与项目作者**
**仓库：`C:\WarwickProjects\AILODResearch`**
**当前阶段：Phase 4 工作区已实现并自动验收；准备进入 Phase 5**

> 本文件是导航、现场快照和执行交接，不是新的模型事实源。模型规则按原始 Implementation Spec → v1.2 → v1.3 → v1.4 → v1.5 的顺序读取；只有后版明确列出的覆盖项优先。

## 1. 新会话必须先完整阅读的文件

按以下顺序完整阅读，不要只看本交接摘要：

1. `Docs/AILOD_MVP_Prototype_Implementation_Spec_CN.md`
2. `Docs/AILOD_MVP_Prototype_Phase_Acceptance_Spec_CN_v1.2.md`
3. `Docs/AILOD_MVP_Prototype_Phase_Acceptance_Spec_CN_v1.3.md`
4. `Docs/AILOD_MVP_Prototype_Phase_Acceptance_Spec_CN_v1.4.md`
5. `Docs/AILOD_MVP_Prototype_Phase_Acceptance_Spec_CN_v1.5.md`
6. 本文件

必须保留 `[LIT]`、`[ADAPTED]`、`[MVP]`、`[TEST]`、`[FROZEN]` 和 `[OPEN]` 的原有含义。不得用本摘要覆盖未被 v1.2—v1.5 明确替代的旧规则。

## 2. 一句话研究方向

在全局最多 50 个 Active Micro NPC 的预算下，比较 Proposed Structured Macro、Simple Aggregate 和 Per-Agent Update LOD 的大规模性能，并在 200 人小规模实验中以 Detailed Individual Oracle 为参考，验证 Proposed 能否在减少离屏决策成本的同时保持王国反馈、居民行为和跨 LOD 连续性。

v1.4/v1.5 的关键主张是：

> Proposed 永久保存每名居民的轻量身份和关键连续状态，只聚合离屏决策计算，不聚合掉居民本人。

## 3. 冻结实验矩阵

### 3.1 小规模准确性实验

| 项目 | 冻结设置 |
|---|---|
| 总人口 | 200；A/B 各 100 |
| 方法 | Oracle、Proposed、Simple、Per-Agent |
| 场景 | None、HarvestCap、StateImport、RepairAid |
| Pilot | 5 paired Seeds，不进入正式统计 |
| Formal | 30 paired Seeds |
| 逻辑时长 | 7 日预热 + 60 日正式阶段 |
| Active Micro Cap | 全局 50 |
| 离屏步长 | Proposed/Simple/Per-Agent 均为 1 游戏小时 |
| 正式运行数 | `4 × 4 × 30 = 480` |

### 3.2 大规模性能实验

| 项目 | 冻结设置 |
|---|---|
| 总人口 | 2k、10k、20k；A/B 均分 |
| 方法 | Proposed、Simple、Per-Agent；不运行 Oracle |
| 重复 | 每配置 10 次 |
| 真实时间 | 60 秒 warm-up + 300 秒 measurement |
| 正式运行数 | `3 × 3 × 10 = 90` |
| 场景 | 当前候选为 Earthquake + StateImport；按原规格在正式性能实验前最终冻结 |

Phase 5 的 67 游戏日规模冒烟只证明程序能够完成，不是正式性能 Run，也不能生成论文性能结论。

## 4. 四种方法的公平边界

| 方法 | 正式角色 | 必须保持的边界 |
|---|---|---|
| Oracle | 200 人准确性参考 | 全部逻辑居民逐人运行完整 Utility + GOAP；不跑大规模 |
| Proposed | 被研究方法 | 全员轻量 CoreState；CohortManaged 批量规划；Active Micro 逐居民规划；离屏不逐人运行 GOAP |
| Per-Agent | 现实性能基线 | 全员独立 ID/状态；每小时逐居民更新；不得聚合 Cohort |
| Simple | 聚合能力下限 | 只保存冻结的王国总量、HomeState 总量和平均延迟队列；不得拥有完整 CoreState、条件分布或个人事件进度 |

统一运行框架不代表四种方法采用相同离屏算法。统一的是场景、政策、动作含义、时间顺序、资源提交、输入和观测；方法差异仍必须保留。

## 5. v1.4 与 v1.5 已批准的架构决定

### 5.1 身份与 LOD

- 所有 Proposed、Oracle 和 Per-Agent 居民从 Manifest 生成起拥有稳定非零 `PersistentID` 与确定性 Name。
- MVP 映射：`PersistentID=ResidentID`，`Name=Resident-%06lld`。
- 固定 20 人只是正式连续性样本，不是系统身份上限。
- Proposed 表示状态为 `CohortManaged XOR ActiveMicro`；CoreState 是否存在不再由 LOD 决定。
- 任意居民可动态切换；Active Micro 同时最多 50 人。

### 5.2 状态与计算分离

- CoreState、Ledger、Event Store 分别保存自己的权威事实。
- Cohort 是可重算的计算缓存，不得成为个人资源或身份的第二真相。
- LOD 升降不得删除或重抽身份、资源、EventID、ReservationID、ArriveID 和剩余进度。
- Blueprint、UI 和 Actor 只读，不得回写确定性研究状态。

### 5.3 Phase 5 统一运行路径

每次 Run 采用以下最小结构：

```text
一个权威运行上下文
  Clock / Scheduler / Scenario / Policies / Activation Trace
  Ledger / Reservation Store / Event Store
  共享动作判定、竞争与提交

一个方法后端
  Oracle | Proposed | Per-Agent | Simple
```

- 每次 Run 只能有一套 Clock、Scheduler、Ledger、Reservation/Event Store 和小时主循环。
- 方法后端只决定保存何种状态、何时规划、是否批量规划。
- Work、BuyWood、ChopWood、Start/ContinueRepair、Routine 和 Wait 的前置条件、费用、持续时间与效果只能定义一次。
- 先接通 Oracle 与 Proposed，再接 Per-Agent 和 Simple。

### 5.4 新批准的时间和事件语义

- 到达 `D60T00:00` 后，先结算全部 `ExecuteAt <= D60T00:00` 的既有事件，再 Audit 和生成最终 Snapshot。
- 不在终点生成新一轮计划，不执行终点之后的事件。
- Repair Capacity 按王国和自然日独立，公式仍为 `floor(0.01N)`，其中 `N` 为单个王国人口。
- Cohort 只批量计算计划；胜者确定后，每名实际执行居民拥有独立 EventID、Owner、进度、ReservationID 和 ArriveID。
- 动作提交不得留下半扣资源、孤立 Event 或没有完成回调的 Scheduler 项。
- Active Micro 与 CohortManaged 候选必须进入同一个有明确王国、资源/容量及时间窗口的竞争集合。

## 6. 当前 Git 现场

此快照生成于 2026-08-16；新会话必须重新运行只读检查，不得假定它仍完全相同。

- 当前分支：`phase-4-state-preserving-cohort`
- 当前 HEAD：`618d851169093621a1e0fe8cd63aa231662ad1b3`
- HEAD 提交：`Implement Phase 3 individual Oracle and GOAP`
- 当前分支没有 upstream。
- 远端：`origin=https://github.com/Jadey0804/AILODResearch.git`
- Phase 4 尚未暂存、提交或推送；它当前只存在于工作区。

生成本交接文档后的预期工作区文件如下；必须用实际 `git status --short` 再核对：

```text
 M Docs/AILOD_MVP_Prototype_Phase_Acceptance_Spec_CN_v1.3.md
 M Source/AILODResearch/Simulation/AILODDomainRules.h
 M Source/AILODResearch/Simulation/AILODIndividualSimulation.cpp
 M Source/AILODResearch/Simulation/AILODIndividualSimulation.h
 M Source/AILODResearch/Simulation/AILODPhase0Manifest.cpp
 M Source/AILODResearch/Simulation/AILODPhase0Types.h
 M Source/AILODResearch/Simulation/AILODSimulationCore.h
 M Source/AILODResearch/Tests/AILODPhase0Tests.cpp
?? Docs/AILOD_MVP_Prototype_Phase_Acceptance_Spec_CN_v1.4.md
?? Docs/AILOD_MVP_Prototype_Phase_Acceptance_Spec_CN_v1.5.md
?? Docs/AILOD_MVP_Prototype_Phase5_Handoff_CN_v1.0.md
?? Source/AILODResearch/Simulation/AILODStatePreservingLOD.cpp
?? Source/AILODResearch/Simulation/AILODStatePreservingLOD.h
?? Source/AILODResearch/Tests/AILODPhase4Tests.cpp
```

特别注意：v1.3 的未提交改动记录了过去“20 人互动池上限”的决定；v1.4 已明确覆盖它。不得静默删除或还原该历史记录，也不得把它当成当前最终规则。

## 7. 阶段 0—4 已完成什么

### Phase 0：确定性输入

- 生成 Initial Population Manifest、Earthquake Damage List 和固定 20 人连续性样本。
- 支持总人口 200、2k、10k、20k。
- v1.4 下全部居民都有稳定身份与 Name。

### Phase 1：纯数据核心

- Clock、Scheduler、Ledger、Reservation Store、Event Store。
- 资源守恒、负库存、重复事务、事件 Owner 与重复完成检查。

### Phase 2：历史 Structured Macro

- 四个场景可完成 7 日预热加 60 日正式模拟。
- 20k StateImport 回归通过。
- 这是旧阶段的 Structured Macro 特征基线，不是 v1.5 Proposed 的端到端完成证据。

### Phase 3：Detailed Individual Oracle

- 200 人最小 Utility + GOAP。
- Work、Buy、Chop、Repair、Wait；整数购买、RepairCredit 优先付款和确定性竞争。
- 四场景 60 日回归通过。

### Phase 4：State-Preserving LOD Bridge

- 全员轻量 CoreState。
- `CohortManaged XOR ActiveMicro`。
- 任意居民动态切换和全局 Active Cap=50。
- Cohort 可由权威状态重算。
- 维修进行到 50% 时的事件、资源和进度连续。
- 统一竞争中的 ArriveID 与 `ResidentID + Action` 绑定；已提交 ID 不得复用；Repair 不得绕过竞争队列。

## 8. 最新验证证据

- UE：5.4.4，Development Editor。
- 最新 NullRHI 自动化日志：`Saved/Logs/AILODResearch.log`。
- `AILODResearch` 共发现 14 项测试：14 Success、0 Failed，退出码 0。
- Phase 4 六项：
  - `Determinism`
  - `DynamicActivationAndCap`
  - `FormalContinuityTrace`
  - `MacroBatchPlanning`
  - `RepairMidpointRoundTrip`
  - `UnifiedCompetition`
- `ConfigHash=7ECCEDAC87BF250D35DE738D53F0D709637804C5`。
- Phase 0 SHA-256 与 Phase 2/3 Digest 统一引用 v1.4 第 7.4 节，避免在交接文档制造第二份可能漂移的结果表。

这些测试证明组件与 Bridge 数据层正确，不证明完整 v1.5 Proposed 已经能够跑正式实验。

## 9. 当前仍未完成的关键能力

1. Structured、Oracle、Phase 4 Bridge 目前仍是三个独立推进路径。
2. v1.5 Proposed 尚未接入完整政策、价格、Forest/Market/Treasury、全部个人动作和完整 67 游戏日主循环。
3. Active Micro 尚未在 Proposed 主循环中持续产生逐居民行为。
4. Domain Rules 目前只统一了参数、金币取整和竞争顺序；动作执行效果仍有重复实现。
5. Day 60 终点事件结算尚未按 v1.5 实现。
6. Repair Capacity 尚未改为每国独立。
7. Ledger、Event、CoreState、Scheduler 尚未统一成无半提交的动作入口。
8. Proposed 批量规划仍可能每小时全扫居民、逐人查 Ledger 并构造字符串；尚无正式性能证据。
9. 旧 `FStructuredMacroRunner` 的序列化结果会标记为 `Proposed`；Phase 6 前必须确保它不会被误当成最终 v1.5 Proposed。
10. Phase 5 Baselines、Phase 6 Runner/指标、Phase 7 UI/望远镜/Actor、Phase 8 Pilot 与正式数据均未实现。

## 10. 关键源码导航

| 文件 | 当前作用 |
|---|---|
| `Source/AILODResearch/Simulation/AILODPhase0Types.h` | Spec/Schema、冻结常量、身份类型 |
| `Source/AILODResearch/Simulation/AILODPhase0Manifest.cpp` | 人口、地震清单、连续性样本生成 |
| `Source/AILODResearch/Simulation/AILODSimulationCore.h/.cpp` | Clock、Scheduler、Ledger、Reservation、Event、Audit |
| `Source/AILODResearch/Simulation/AILODDomainRules.h` | 共享参数、整数付款、确定性竞争 Key；尚不是完整动作执行器 |
| `Source/AILODResearch/Simulation/AILODStructuredMacro.h/.cpp` | 旧 Phase 2 Structured Macro Runner |
| `Source/AILODResearch/Simulation/AILODIndividualSimulation.h/.cpp` | Oracle、个体 CoreState、Utility + GOAP |
| `Source/AILODResearch/Simulation/AILODStatePreservingLOD.h/.cpp` | Phase 4 Bridge、LOD、Cohort 缓存、维修与统一竞争 |
| `Source/AILODResearch/Simulation/AILODLogSchema.h` | 冻结日志文件名和字段；不得随意修改 |
| `Source/AILODResearch/Tests/AILODPhase0Tests.cpp` | Phase 0 确定性与全员身份 |
| `Source/AILODResearch/Tests/AILODPhase1Tests.cpp` | 核心契约 |
| `Source/AILODResearch/Tests/AILODPhase2Tests.cpp` | 旧 Structured 四场景与规模回归 |
| `Source/AILODResearch/Tests/AILODPhase3Tests.cpp` | Oracle 与 GOAP 回归 |
| `Source/AILODResearch/Tests/AILODPhase4Tests.cpp` | Bridge、动态切换、维修与竞争回归 |

UE 内容仍基本是 Third Person 模板。AILOD 专用地图、研究 Actor、Widget、望远镜和运行时 Subsystem 尚未建立，这是 Phase 7 的预期缺口，不是 Phase 5 任务。

## 11. Phase 5 推荐实施顺序

### 入口保护

1. 重新检查分支、HEAD、所有未提交改动和最新日志。
2. 重新编译并运行现有 14 项测试。
3. 核对 Phase 4 检查点的准确文件范围。
4. 在获得提交授权后，将当前 Phase 4 与 v1.4/v1.5/交接文档提交在 `phase-4-state-preserving-cohort`；不要 push。
5. 再创建并切换到 `phase-5-unified-backends`；禁止使用 `codex/` 前缀。

### Phase 5A：统一运行路径

1. 为旧 Oracle/Structured 输出保留特征回归，防止迁移时无意改变冻结语义。
2. 建立每次 Run 唯一的时间、场景、政策、Ledger/Event 和小时推进路径。
3. 提取共享动作判定、竞争与提交入口。
4. 先接 Oracle，单独实现并记录批准的 Day 60 边界变化。
5. 再接 Proposed，使其完成 `Day -7 → Day 60`；CohortManaged 与 Active Micro 均持续运行。
6. 修复每国维修容量、权威竞争作用域和最小原子提交边界。

### Phase 5B：两个 Baseline

1. 接入 Per-Agent：复用个体动作执行，离屏每小时逐居民更新，不聚合。
2. 接入 Simple：只使用冻结总量与平均延迟队列，不获得个人状态能力。
3. 增加公平性、确定性、失败边界、终点和规模冒烟测试。

不要先实现 Simple 后再补统一框架，否则会把现有三套规则扩大成四套。

## 12. Phase 5 Definition of Done

- 200 总人口下，四方法 × 四场景均由统一路径完成 7 日预热和 60 日正式阶段；使用固定测试 Seed，不替代正式 30 Seeds。
- 四方法共享 Manifest、Damage List、政策时间表、小时顺序、Domain Rules、确定性随机规则和固定 Activation Trace。
- Proposed 的 CohortManaged 与 Active Micro 居民都持续产生合法行为；Active Micro 始终不超过 50。
- 两种表示下的候选进入同一权威竞争集合，观察状态不改变优先级。
- Simple、Per-Agent、Proposed 在 2k、10k、20k 下至少完成一次 `StateImport` 全逻辑时段冒烟；不宣称性能结论。
- 每国维修容量独立，公式仍为 `floor(0.01N)`。
- 最终 Snapshot 前不存在 `ExecuteAt <= D60T00:00` 的未结算事件。
- 失败动作不留下半扣资源、孤立事件或无完成回调的 Scheduler 项。
- 人口、Wood、Coin、Event Owner、Reservation、DuplicateTransaction、DuplicateCompletion 和 TaskReset 硬错误均为 0。
- 对每个实际 Cohort 验证批量计划与组内每名居民逐人计算的计划一致。
- Phase 0—4 的 14 项测试继续通过；所有 Digest 变化逐项说明。
- 只记录规划次数、扫描数、Ledger 查询数和事件/事务数量；不设性能门槛。
- 正式生产路径不再调用一套含义不同的旧政策或动作实现。
- Phase 5 完成时将 `SpecVersion` 更新为 `1.5`，`SchemaVersion` 仍为 `1.1`，并记录新 ConfigHash/输入 Hash。

## 13. Phase 5 明确禁止事项

- 不提前实现 Phase 6 Runner、正式 CSV/JSONL 落盘或离线指标。
- 不提前实现 Phase 7 UI、望远镜、地图、Actor 或 Blueprint 表现。
- 不运行 480/90 次正式实验。
- 不加入玩家跨国运输任务、动态国王、Food、LLM、PCG、完整社交或额外经济。
- 不改变 `[FROZEN]` 人口、场景、公式、Seeds、Runs、Active Cap、方法边界或日志字段。
- 不增加第五种消融方法。
- 不把系统级结果写成 Cohort 单一机制的因果证明。
- 不把自动化连续性写成“玩家一定看不出穿帮”。
- 不为了制造性能优势而放慢 Per-Agent 或提前复杂化 Proposed。
- 不删除旧 Digest，不把旧 Phase 2 输出冒充最终 Proposed。
- 不 reset、stash、覆盖或批量丢弃当前未提交文件。
- 未经明确授权不 push。

## 14. 验证命令与注意事项

### 14.1 只读 Git 核对

```powershell
git branch --show-current
git rev-parse HEAD
git status --short
git diff --check
```

仓库现有换行设置可能显示 LF → CRLF 警告；只要没有 whitespace error，不应为此机械改写全部文件。

### 14.2 UE5.4 Development 编译

运行前先确认 Unreal Editor 和 Live Coding 没有占用模块。

```powershell
& 'D:\ruanjian\Unreal Engine\UE_5.4\Engine\Build\BatchFiles\Build.bat' `
  AILODResearchEditor Win64 Development `
  'C:\WarwickProjects\AILODResearch\AILODResearch.uproject' `
  -WaitMutex -NoHotReloadFromIDE
```

### 14.3 NullRHI 自动化测试

```powershell
${env:UE-LocalDataCachePath}='C:\WarwickProjects\AILODResearch\Saved\DerivedDataCache'
& 'D:\ruanjian\Unreal Engine\UE_5.4\Engine\Binaries\Win64\UnrealEditor-Cmd.exe' `
  'C:\WarwickProjects\AILODResearch\AILODResearch.uproject' `
  -unattended -nop4 -nosplash -NullRHI `
  '-ExecCmds=Automation RunTests AILODResearch; Quit' `
  '-TestExit=Automation Test Queue Empty' -log
```

不要只以进程退出码判断成功；还要在 `Saved/Logs/AILODResearch.log` 中核对发现的测试数、每项 `Result={Success}`、失败数和最终退出码。

## 15. 研究结论的严格边界

- Oracle 回答的是新版 Proposed 与详细个体模拟相差多少，不是大规模“绝对真实世界”。
- Simple 与 Proposed 同时改变了状态保留和聚合结构，因此只能比较完整系统方案。
- 连续性硬门证明的是已建模身份、资源、行为上下文和承诺事件不丢失。
- “玩家是否察觉穿帮”需要用户研究；当前只能记录 Potential BIR 和可测连续性。
- Proposed 的 CoreState 内存为 `O(N)`；性能收益必须来自实测，不能从设计直接宣布。
- Phase 5 的规模冒烟只证明可运行；正式 CPU、内存、P95 和置信区间属于 Phase 8 数据。

## 16. 建议复制到新会话的首条指令

```text
请先完整阅读以下文件，不要立即写代码：

1. Docs/AILOD_MVP_Prototype_Implementation_Spec_CN.md
2. Docs/AILOD_MVP_Prototype_Phase_Acceptance_Spec_CN_v1.2.md
3. Docs/AILOD_MVP_Prototype_Phase_Acceptance_Spec_CN_v1.3.md
4. Docs/AILOD_MVP_Prototype_Phase_Acceptance_Spec_CN_v1.4.md
5. Docs/AILOD_MVP_Prototype_Phase_Acceptance_Spec_CN_v1.5.md
6. Docs/AILOD_MVP_Prototype_Phase5_Handoff_CN_v1.0.md

然后检查当前 Git 分支、HEAD、全部未提交改动和最新自动化日志。注意：Phase 4 当前可能仍只存在于 phase-4-state-preserving-cohort 的工作区，不能 reset、stash、覆盖或直接从旧 HEAD 开 Phase 5。

先向我报告：
- 交接文档与实际仓库是否一致；
- Phase 4 是否仍能编译并通过现有 14 项测试；
- 拟纳入 Phase 4 检查点的准确文件清单；
- 是否存在会实质改变模型或实验的未决问题。

确认 Phase 4 检查点后，再创建并切换到 phase-5-unified-backends；不要使用 codex/ 前缀，不要 push。只实施 Phase 5，先建立统一运行路径，再依次接入 Oracle、Proposed、Per-Agent 和 Simple。不得改变 [FROZEN] 参数、公式、方法边界或日志字段；不得提前实现 Phase 6/7/8。每完成一个可验证小步都运行相关测试，最终严格按 v1.5 的 Phase 5 DoD 验收。
```

## 17. 新会话第一次回复应达到的标准

新的 Codex 不应立刻开始重构。第一次回复至少应给出：

1. 实际分支、HEAD、未提交文件与文档快照；
2. 文档冲突和 v1.5 覆盖关系；
3. 现有 14 项测试的现场复验结果；
4. Phase 5A 的最小文件级实施计划；
5. 只询问会改变模型或实验的未决问题；
6. 明确不会提前做 UI、Runner、正式性能实验或额外玩法。
