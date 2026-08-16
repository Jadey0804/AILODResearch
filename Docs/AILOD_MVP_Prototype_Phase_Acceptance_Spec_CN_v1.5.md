# Hierarchical AI / Simulation LOD：MVP v1.5 统一运行架构与阶段 5 入口规格

**版本：v1.5**
**日期：2026-08-16**
**状态：架构方向与下列语义已由项目作者批准；Phase 0—4 桥梁数据层已验收，Phase 5—8 未实施**
**基准文档：`AILOD_MVP_Prototype_Implementation_Spec_CN.md` v1.1**
**前序修订：v1.2、v1.3、v1.4 阶段验收规格**
**用途：冻结 Phase 5 的统一运行路径、两项终点/事件语义和快速验收标准。历史文档保留不改。**

## 1. 文档优先级与不变项

实现依次读取 v1.1、v1.2、v1.3、v1.4 和本文件。本文件只覆盖明确写出的冲突；未覆盖内容继续有效。

- 不改变既有 `[FROZEN]` 人口规模、Active Micro Cap、场景、政策数值、公式、小时顺序、Seeds、Runs、主要指标和日志字段。
- 不加入玩家运输任务、动态国王、Food、LLM、PCG、完整社交或额外经济复杂度。
- C++ 继续负责全部确定性研究逻辑；Blueprint、UI 和 Actor 只负责配置与表现。
- 每次只实施一个阶段；Phase 5 通过本文件 DoD 前不得进入 Phase 6。
- 当前 `SpecVersion` 仍为 `1.4`。Phase 5 完成 v1.5 语义后再统一改为 `1.5`；`SchemaVersion` 保持 `1.1`。由 SpecVersion 引起的 ConfigHash 和输入文件 Hash 变化必须记录，不得误写为领域行为变化。

## 2. 当前结论：方向成立，但完整 Proposed 尚未形成

v1.4 的核心方向继续有效：所有居民永久保留轻量 CoreState，Cohort 只聚合决策计算，Active Micro 与 CohortManaged 使用同一身份、资源和事件事实。

当前自动化测试已经证明：

- 身份和个人资源不会因为零时间 LOD 往返而丢失；
- 维修进行到 50% 时，EventID、ArriveID、Owner、剩余时间和 4 Wood 结算保持连续；
- 任意居民均可动态切换，Active Micro 全局上限仍为 50；
- Macro/Micro 候选可以进入同一确定性竞争队列。

这些结果只证明 **Phase 4 Bridge 数据结构与局部维修链路**。它们尚未证明新版 Proposed 已在完整政策、价格、王国库存和居民行为下运行 7 日预热加 60 日正式阶段。

Phase 5 入口处仍有三个独立推进路径：旧 Structured Macro、Detailed Individual Oracle 和 Phase 4 State-Preserving LOD。它们不能直接作为正式四方法实验的三个独立事实源。

## 3. Phase 5 批准的统一运行架构

### 3.1 每次 Run 只有一套权威运行状态

每个方法的每次 Run 必须只有一个：

- 权威 Clock 与小时推进顺序；
- Scheduler；
- Scenario 与政策时间表；
- Ledger；
- Reservation Store 与 Event Store；
- 稀缺资源竞争入口；
- 固定 Activation Trace；
- Audit 与只读观测出口。

“四方法共用”是指共用相同实现、输入和事件顺序，不是四个方法在同一次 Run 中共享同一个对象。

### 3.2 方法只改变 LOD 研究变量

| 方法 | 允许改变的内容 | 禁止偷渡 |
|---|---|---|
| Oracle | 200 人全部逐居民 Utility + GOAP | 不作为 20k 部署方法，不要求 200 个可见 Actor |
| Per-Agent | 全部居民保留个体状态；离屏每游戏小时逐居民更新 | 不得按 Cohort 聚合决策 |
| Simple | 仅保存冻结的王国总量、HomeState 总量和平均延迟队列 | 不得获得完整 CoreState、条件分布或个人事件进度 |
| Proposed | 全员轻量 CoreState；CohortManaged 批量规划；Active Micro 逐居民规划 | 离屏不得逐居民运行 GOAP；观察状态不得获得资源优先级 |

各方法只能改变状态表示、规划粒度和离屏更新方式。政策、动作含义、费用、持续时间、资源效果、竞争顺序和小时管线不得各写一份。

### 3.3 共享动作与提交路径

Work、BuyWood、ChopWood、StartRepair、ContinueRepair、Routine 和 Wait 的以下内容必须只有一个 C++ 定义：

- 前置条件；
- 费用与整数取整；
- 持续时间；
- 资源转移；
- Event/Reservation 创建与完成；
- CoreState 结果；
- 失败后的 Wait 或拒绝结果。

Oracle、Per-Agent 和 Active Micro 逐居民调用；Proposed 的 CohortManaged 批量计算候选，但最终仍通过同一个竞争和提交入口。

### 3.4 Active Micro 不能停止模拟

Proposed 的每个小时必须同时处理：

1. CohortManaged 居民的批量规划；
2. Active Micro 居民的逐居民规划；
3. 两类候选人的统一竞争；
4. 统一动作提交。

Active Micro 居民不得因为从 Cohort 中移除而停止产生行为，也不得被 Cohort 重复计算。

## 4. 已批准的语义澄清与修正

### 4.1 Day 60 终点 `[APPROVED OVERRIDE]`

到达 `D60T00:00` 后：

1. 结算全部 `ExecuteAt <= D60T00:00` 且尚未完成的既有事件；
2. 不在 `D60T00:00` 生成新一轮居民计划；
3. 不执行 `D60T00:00` 之后的事件；
4. 最后执行 Audit 并生成最终 Snapshot。

小时步数仍是 7 日预热加 60 日正式阶段；本条只澄清终点结算，不改变正式时长或冻结小时顺序。旧 Oracle Digest 可能因此变化。只能以该原因建立新的 v1.5 基线，并保留旧 Digest 作为历史证据。

### 4.2 Repair Capacity 按王国独立

冻结公式仍为：

> `RepairStartCapacity(Kingdom, Day) = floor(0.01 * N)`

其中 `N` 是单个王国人口。额度按 `{Kingdom, natural day}` 独立重置、不跨日累计；A 国开工不得消耗 B 国额度。

### 4.3 Cohort 批量规划、每居民独立 Event `[APPROVED OVERRIDE]`

- Cohort 只合并计划计算和候选生成。
- 稀缺资源胜者确定后，每名实际执行居民拥有独立 EventID、Owner、进度、ReservationID 和 ArriveID。
- 多名居民不得共享一个可被单个居民 LOD 切换整体转移的权威 EventID。
- 批次可以作为只读统计或索引，但不得取代居民自己的承诺事件。
- 每居民 EventID 不等于每居民 Actor；离屏居民仍不创建表现 Actor。
- 不增加、删除或改名冻结日志字段。

### 4.4 动作提交不得留下半完成状态

动作开始前先验证全部条件和额度。提交成功后，Ledger、Event、Scheduler、CoreState 和容量消费必须相互一致；失败不得遗留已扣资源、孤立 Pending Event 或没有完成回调的 Scheduler 项。

不要求为 MVP 建造通用数据库事务框架，但必须使用一个共享提交入口，并为每个可失败边界增加自动测试。

### 4.5 竞争必须绑定真实作用域

竞争不能只接收调用者随意传入的数字。每次竞争必须明确：王国、资源或容量类型、自然日/小时窗口及权威可用量。相同作用域不能被分两次完整分配；Active Micro 与 CohortManaged 候选必须进入同一集合。

## 5. 统一小时管线

继续使用 v1.3 已冻结顺序：

> 到期事件 → 当日政策 → Growth → Baseline Import → Harvest → Routine Consumption → Price → 居民规划/竞争/提交 → Audit/Log

- Day 0 地震仍发生在第一个正式 Flow 前。
- 所有方法读取同一时间点的世界事实。
- 推进到任意中途时刻时，按时间顺序完成所有到期事件后再提交权威 Clock；不得让较早事件读取一个已经跳到未来的 Clock。

## 6. Phase 5 实施顺序

1. 建立唯一小时主循环和最小 Backend 接口。
2. 提取共享 Scenario、Policy 和动作判定/提交入口。
3. 先接入 Oracle；除批准的 Day 60 澄清外，保持既有冻结行为。
4. 接入 Proposed，使其真实完成 `Day -7 → Day 60`；CohortManaged 批量规划、Active Micro 逐居民规划。
5. 修复每国维修容量、真实竞争作用域和最小原子提交边界。
6. 接入 Per-Agent；复用个人动作执行器，只改变离屏调度。
7. 最后接入 Simple；严格保持总量/均值能力下限。
8. 增加公平性、确定性、终点边界和规模冒烟测试。

旧 Runner 可以暂时保留为历史特征回归，但不得在 Phase 6 中被错误标记为最终 v1.5 Proposed。

## 7. Phase 5 Definition of Done

- 200 总人口下，四方法 × 四场景均由统一运行路径完成 7 日预热和 60 日正式阶段；只使用固定测试 Seed，不替代 30 个正式 Seeds。
- 四方法共享同一 Manifest、Damage List、政策时间表、小时顺序、Domain Rules、确定性随机规则和固定 Activation Trace。
- Proposed 的 CohortManaged 与 Active Micro 居民都持续产生合法行为；Active Micro 始终不超过 50。
- Active Micro 与 CohortManaged 的稀缺资源候选进入同一竞争集合，观察状态不改变优先级。
- Simple、Per-Agent、Proposed 在 2k、10k、20k 下至少完成一次 `StateImport` 全逻辑时段冒烟；这只证明可运行，不是正式性能结果，也不冻结 Phase 8 的主压力场景选择。
- 每国维修容量独立，公式保持 `floor(0.01N)`。
- 最终 Snapshot 前不存在 `ExecuteAt <= D60T00:00` 的未结算事件。
- 失败动作不留下半扣资源、孤立事件或无完成回调的 Scheduler 项。
- 人口、Wood、Coin、Event Owner、Reservation、DuplicateTransaction、DuplicateCompletion 和 TaskReset 硬错误均为 0。
- 对每个实际 Cohort 验证：批量代表计划与组内每名居民按同一规则单独计算的结果一致。
- Phase 0—4 现有 14 项自动化测试继续通过；任何 Digest 变化必须逐项说明来源。
- 记录规划次数、全人口扫描数、Ledger 查询数及事件/事务数量作为诊断，不设置性能通过门槛，不宣称 Proposed 已经更快。
- 生产实验路径不再依赖一套含义不同的旧政策或动作实现。

## 8. Phase 5 明确不做

- 不实现 Phase 6 正式 Runner、CSV/JSONL 落盘或离线指标。
- 不实现 Phase 7 UI、望远镜、地图、Actor 或 Blueprint 表现。
- 不运行 480 次准确性正式 Runs 或 90 次性能正式 Runs。
- 不加入玩家运输任务、动态国王、Food、LLM、PCG、社交系统或额外经济。
- 不改变 `[FROZEN]` 人口、场景、公式、Seeds、Runs、Active Cap、方法边界或日志字段。
- 不为了制造性能优势而故意放慢 Per-Agent，或在测量前为 Proposed 增加复杂索引。

## 9. 研究主张与性能边界

- Proposed 的 CoreState 内存仍为 `O(N)`；潜在收益主要来自减少离屏 GOAP、逐居民调度、感知、Actor 和 Tick。
- 每居民独立 Event 会增加事件状态数量，这是换取强连续性的明确成本，必须纳入后续内存测量。
- Proposed 每小时全量扫描、逐居民 Ledger 查询和字符串分组可能成为瓶颈；Phase 5 先记录成本，只有证据表明它是瓶颈后才能优化。
- Simple 与 Proposed 同时存在多项结构差异，因此论文只报告“完整系统方案的性能—准确性权衡”，不把全部提升单独归因于 Cohort batching。
- 自动连续性测试只能证明已建模字段没有丢失，不能等同于“玩家一定看不出穿帮”。若以后要作玩家感知主张，需要独立用户研究；不加入本次 MVP 正式矩阵。

## 10. 当前回归基线与版本迁移

- 当前实施分支：`phase-4-state-preserving-cohort`。
- 当前代码 `SpecVersion=1.4`、`SchemaVersion=1.1`。
- UE 5.4 Development Editor 编译通过；NullRHI 全套 `AILODResearch` 自动化测试 14/14 Success，其中 Phase 4 为 6/6 Success。
- Phase 0 Hash、Phase 2/3 Digest 与 Phase 4 覆盖范围以 v1.4 第 7.4 节为准，不在本文件复制成第二份事实。
- v1.5 实施前的 14 项测试是必须保留的回归基线；它们证明组件行为，不证明完整 v1.5 Proposed 已完成。

## 11. v1.5 决策记录

| 版本 | 日期 | 决定 | 原因与影响 | 批准人 |
|---|---|---|---|---|
| v1.5 Runtime | 2026-08-16 | Phase 5 先统一每次 Run 的 Clock、Scenario、Domain、Ledger/Event 与竞争路径，再接入四个 Backend | 防止四方法各自复制规则，使实验差异可归因于 LOD 方法 | 项目作者 |
| v1.5 End Boundary | 2026-08-16 | D60 最终快照前结算所有 `ExecuteAt <= D60T00:00` 的既有事件，不在终点生成新计划 | 消除最终状态的一小时歧义；可能需要重建并解释旧 Digest | 项目作者 |
| v1.5 Event Granularity | 2026-08-16 | Cohort 批量规划，但每名实际执行居民保留独立权威 Event | 避免一名居民切换 LOD 时夺走整批事件 Owner；代价是事件状态内存增加 | 项目作者 |
| v1.5 Claims | 2026-08-16 | MVP 不增加第五种消融方法；只作系统级方案比较，不作玩家感知结论 | 控制范围，并避免超出证据的因果与体验主张 | 项目作者 |
