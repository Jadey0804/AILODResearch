# Hierarchical AI / Simulation LOD：MVP v1.9 住房连续性修正规则

**版本：v1.9**  
**日期：2026-08-21**  
**分支：`phase-6i-home-continuity`**  
**基线提交：`4e11072`（Phase 6H-H5）**  
**状态：项目作者已于 2026-08-21 采纳本方向；实现与复验进行中。**  
**用途：修正居民靠近玩家时，房屋损坏和维修进度可能被恢复错的问题。**

## 1. 这次到底要解决什么

H5 发现：同一名居民在 Oracle 里可能还在等维修或正在维修，但 Proposed 把他恢复成了“房屋完好”，于是他靠近玩家后的第一件事也跟着变了。

v1.9 不让远处的每个居民重新每小时独立思考。它只额外记住两件很小的事实：

1. 每栋房现在是完好、等维修、维修中还是已修好；
2. 维修中的房屋属于哪一条批量维修任务。

大规模社会仍按群体一次计算，只有房屋真的发生变化时，才按固定顺序更新对应房屋。

## 2. 明确覆盖哪些旧规则

本文件只覆盖 v1.7 中以下冲突项：

- v1.7 §3、§4 中“未激活人口的全部当前动态事实只由 Joint State 保存”：改为 Joint State 继续保存离屏社会的权威总量和决策状态，同时增加一份每房屋 1 个紧凑状态值，专门保证 HomeID 连续性；
- v1.7 §4 中“Identity Registry 不保存当前 HomeState”：静态 Identity 仍不直接保存当前 HomeState，但权威会话增加独立的 Home Continuity Registry；
- v1.7 §9、§10 中仅靠 Capsule 与 Joint Cell 恢复 HomeState：Lift 时必须先读取该居民 HomeID 的权威房屋状态；房屋正在维修时，还必须接回对应的批量维修任务。

没有在本文件明确覆盖的 v1.7、v1.8 规则继续有效。

## 3. 不能退回逐居民模拟

- 每小时仍按非空 Joint Cell 规划一次，不能按人数生成个人候选；
- Macro 与 Active 仍通过同一批量竞争、账本、预约和事件入口；
- 一次批量维修仍只有一条 Batch Event，不能为整批每个人复制一条事件或交易；
- 房屋状态只在初始化、地震、维修开始、维修完成和玩家附近 Lift/Restrict 时读取或改变；
- 禁止为了维护房屋状态而每小时扫描所有 Identity。

允许的额外工作是 `O(H_changed)`：如果这一小时确实有 20 栋房开始或完成维修，就更新这 20 个紧凑状态。它不是让全部人口重新思考，也不生成 20 条个人事件。这个成本必须单独计数并在 20k、100k 检查中验证。

## 4. 房屋状态怎样保存

- 每个唯一 HomeID 对应一个紧凑状态槽；当前 MVP 使用数组槽，不在每个 Identity 中复制一份可变 HomeState；
- Identity 只保存这个状态槽的编号和原有静态身份；
- 状态值只允许 `Healthy / DamagedWaiting / UnderRepair / Repaired`；
- 每个状态槽必须且只能属于一个 HomeID；
- Home Continuity Registry 的人数和各外层 Cohort 的房屋状态总数，必须与 Joint Cell、进行中 Batch Event 和 Active 的权威总数一致；不一致属于硬错误，不能当作近似误差。

## 5. 地震和批量维修怎样对应到具体房屋

### 5.1 地震

地震仍按 Cohort 批量搬动人数和资源；同时按 Phase 0 已冻结的 Damage List，把名单里的具体 HomeID 改成 `DamagedWaiting`。两边的王国、职业、收入段和房屋状态人数必须对得上。

### 5.2 维修开始

- Active 居民开始维修：直接把他自己的 HomeID 改成 `UnderRepair`；
- Macro 批量开始维修：在相同外层 Cohort 的 `DamagedWaiting` 房屋中，按固定 ResidentID 顺序选出获批数量；
- 如果某栋房的居民此刻处于 Active，就暂时跳过，不能让同一栋房同时归个人任务和匿名批量任务所有；
- Batch Event 保存紧凑的房屋状态槽编号列表，不保存完整个人 CoreState，也不产生个人 Event/Ledger；
- 同一输入、Seed、时间和提交顺序必须选中同一批房屋。

### 5.3 维修完成

Batch Event 只把自己持有的房屋从 `UnderRepair` 改成 `Repaired`。事件被 Lift 拆出或 Restrict 合回时，房屋归属必须跟着迁移，不能丢失、重复或被另一条事件提前完成。

## 6. Lift / Restrict 规则

- Lift 先查询 HomeID 的当前房屋状态，再选择相同外层 Cohort、相同 HomeState 的可用 Joint Cell；
- 如果房屋是 `UnderRepair`，必须通过房屋记录找到对应的 Pending Batch Event，并接回剩余时间；不得恢复成 Healthy/Routine；
- Restrict 写回时，Active 状态必须和该 HomeID 的权威房屋状态一致；
- Capsule 继续记录玩家已经观察过的内容，但不能覆盖更新后的权威房屋状态；
- Active 上限继续固定为 50。

## 7. 新增检查和内存口径

新增硬检查：

- HomeID 与状态槽一一对应；
- 住房状态按外层 Cohort 对账为 0；
- `UnderRepair` 房屋与 Pending Repair Event 一一对应；
- Repair Event 拆分、合并和完成后没有房屋归属残留；
- Active 居民看到的 HomeState 与自己的 HomeID 一致。

新增已追踪内存项 `home_continuity_bytes`，包括：

- 每栋房的紧凑状态数组；
- 批量维修的确定性候选顺序和游标；
- 维修中房屋到 Batch Event 的临时归属；
- Batch Event 中房屋状态槽编号数组。

它仍不是整个 UE 进程的绝对内存。

## 8. 连续性指标补充

v1.8 的旧指标全部保留。另加两组只看“玩家会在意的住房承诺”的指标：

- `CommitmentTaskActiveStatusMismatchRate`：只在 Oracle 或 Proposed 任一方处于 RestoreHome 任务时，比较双方是否都还有进行中任务；
- `CommitmentTaskRemainingHoursMAE`：双方都在进行 RestoreHome 任务时，比较还差多少小时完成。

RoutineLife 继续保留在原始通用任务指标里，但不能再用 Routine 的周期错位掩盖 Repair 承诺是否连续。

## 9. H6 验收顺序

1. H6-0：冻结 v1.9 和权威索引；
2. H6-A：实现 Home Continuity Registry、地震名单和批量维修具体房屋对应；
3. H6-B：接入 Lift/Restrict、事件拆分/合并、硬检查和新摘要；
4. H6-C：增加住房承诺任务计时指标和自动测试；
5. H6-D：完整编译、全部自动测试和 5 Seed 预检查；
6. H6-E：20k 速度门和 100k Proposed 内存/压力检查；
7. H6-F：用大白话记录结果，并由项目作者决定是否开放正式模型资格。

任何一步失败都先修正或说明，不得静默降低准确性、Active 上限或资源守恒规则来换取通过。

## 10. 这次验收能证明什么、不能证明什么

通过后可以证明：Proposed 不再因为居民离屏而忘记“他的房还坏着或正在修”，并且这个修正没有把大规模运行退回逐居民每小时计算。

它不能证明：Proposed 与 Oracle 的钱、木材和所有行动完全相同；也不能替代后续正式实验、统计分析或玩家可视化演示。
