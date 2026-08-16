# Hierarchical AI / Simulation LOD：MVP v1.4 架构修订与返工验收规格

**版本：v1.4**
**日期：2026-08-16**
**状态：新架构已由项目作者批准；Phase 0—4 数据层返工已通过自动验收，Phase 5—8 未实施**
**基准文档：`AILOD_MVP_Prototype_Implementation_Spec_CN.md` v1.1**
**前序变动：v1.2、v1.3 阶段验收规格**
**用途：只记录 v1.4 批准的方法边界、必要返工和阶段 4 快速验收标准。历史文档保留不改。**

## 1. 文档优先级与不变项

实现依次读取 v1.1、v1.2、v1.3 和本文。本文只覆盖第 2 节明确列出的冲突；其余较早版本内容继续有效。

- 不改变既有 `[FROZEN]` 人口规模、Active Micro Cap、场景、政策、公式、时间线、Seeds、Runs、指标和日志字段。
- 不加入 LLM、PCG、完整社交、玩家任务、跨国运输、Food 或额外经济复杂度。
- C++ 仍是唯一确定性研究逻辑；Blueprint/UI/Actor 只负责配置和表现。
- 每次只推进一个实施阶段，并按该阶段 DoD 验收。
- Meadows 提供的是系统边界、Stock/Flow、反馈、延迟、层级和信息连接原则；本文的 CoreState、批量决策与竞争队列是项目据此作出的 `[ADAPTED][MVP]` 设计，不声称来自书中现成算法。

## 2. v1.4 批准覆盖的旧边界

| 旧条目 | v1.4 批准替代规则 | 影响 |
|---|---|---|
| v1.1 第 16—18 节：匿名居民激活时从 Cash moments、Wood histogram 随机重建，降级时把个人资源写回匿名桶 | Proposed 中每个逻辑居民永久保留稳定身份与准确轻量 CoreState；LOD 切换不随机重建曾存在的个人资源，也不销毁个人状态 | 原映射仅继续作为 Simple Aggregate 的能力下限，不再用于 Proposed |
| v1.1 `Anonymous Cohort XOR Persistent Macro XOR Active Micro` | Proposed 改为 `CohortManaged XOR ActiveMicro`；CoreState 是否存在不再由 LOD 决定 | Persistent 不再是一种有限名额或第三套 AI |
| v1.3 只有预登记 20 人可识别/互动，运行期不动态扩充 Persistent Pool | Proposed 的任意居民均可动态进入 Active Micro；固定 20 人只保留为正式连续性配对样本 | 系统能力无 20 人上限；同时 Active 仍严格不超过 50 |
| v1.3 匿名居民 `PersistentID=0`、Name 为空 | Proposed/Oracle/Per-Agent 的全部居民拥有稳定非零 `PersistentID` 和确定性 Name；MVP 使用最简映射 `PersistentID=ResidentID`、`Name=Resident-%06lld` | Phase 0 Manifest/Hash 与测试需更新；日志字段名不变 |
| v1.3 开关望远镜不得改变后续全部日志 | 纯 UI/渲染开关不得改变日志；同一时刻零时间 LOD 往返不得改变状态。切换后因批量近似与 GOAP 产生的未来差异属于准确性误差，必须测量 | 正式比较仍使用固定 Activation Trace；动态望远镜只用于演示 |

除上表外，不允许借 v1.4 改动任何旧的 `[FROZEN]` 参数、公式、方法或日志 Schema。

## 3. Proposed v1.4 的权威架构

### 3.1 研究主张

> Proposed 在保存全部居民关键连续状态的前提下，聚合离屏居民的决策计算，而不是聚合掉居民本人；研究验证其能否降低计算成本，并把宏观与行为误差控制在可测范围内。

该主张只适用于 Proposed。Oracle 与 Per-Agent 本来就保留独立个体状态；Simple Aggregate 仍按 v1.1 第 21.1 节只保存总量，不能获得 Proposed 的 CoreState 能力。

### 3.2 每名居民永久保留的轻量 CoreState

| 类别 | 强连续字段 |
|---|---|
| 身份 | ResidentID、PersistentID、Name、HomeID、Kingdom、Profession、IncomeBand |
| 个体事实 | HomeState、Money、RepairCredit、InventoryWood |
| 行为上下文 | CurrentGoal、LastCompletedAction、MacroIntent |
| 已承诺事件 | EventID、ParentEventID、StartTime、RemainingWork、ReservationID、CausalPolicyID、ArriveID |
| 时间/表现锚点 | LastUpdateTime、LocationAnchor、RNGStreamKey、Version |

`Money/RepairCredit/InventoryWood` 必须准确反映 Ledger 账户；序列化副本只能作为经校验的快照，不能成为第二个可写真相。Name 可以按 ID 确定性生成，不要求额外复杂存储。

Repair Start 直接消耗居民已持有的 4 Wood，不需要预留中间库存，因此该事件的 `ReservationID=0` 明确定义为“无 Reservation”，跨 LOD 时必须继续保持 0；BuyWood 等需要预留的动作仍必须保留非零 ReservationID。

MVP 的“记忆连续性”只承诺上表身份、状态、已完成动作和已承诺事件；不承诺尚未建模的完整人生记忆、社交关系或自由对话历史。未承诺的下一步计划、普通行走路线和临时意图允许在激活后重规划。

### 3.3 每类数据只有一个权威来源

| 数据 | 唯一权威来源 | 其他层的权限 |
|---|---|---|
| 身份、HomeState、行为上下文、LOD 状态 | Canonical Resident Store / CoreState | UI、Actor 和 Cohort 只读 |
| Coin、RepairCredit、Wood | Ledger 中的居民/王国/边界账户 | CoreState 只暴露同步后的准确视图 |
| 事件、进度、Owner、Reservation | Event Store / Reservation Store | CoreState 只保存引用与同步进度 |
| Cohort Count、Cash moments、Wood histogram、Intent totals | 从 CoreState、Ledger 和 Event Store 建立的可重算缓存 | 不得独立创造、销毁或覆盖个人事实 |
| 展示数据 | 从上述权威数据读取 | Blueprint/UI 不缓存或回写研究状态 |

每个检查点必须满足：Cohort 缓存可由当前权威状态重新计算，且人口、Coin、Wood、事件 Owner 与事务幂等检查通过。

### 3.4 Cohort 聚合计算，不销毁状态

- `CohortManaged` 居民不运行逐居民 GOAP、Actor Tick、感知或动画。
- Cohort 按会改变动作资格和结果的事实分组，一次计算一批居民的候选行为与数量。
- 批量结果通过固定 Seed、GameTime、ResidentID 和 ActionID 确定性分配给具体居民，再由共享 Domain Rules、Ledger 和 Event Store 提交。
- Active Micro 居民使用同一份 CoreState；升级只创建详细执行状态和 Actor，降级只回收这些昂贵对象。
- 降级后 CoreState、Ledger 账户、EventID、RemainingWork、ReservationID 和 ArriveID 不删除、不重抽、不重新开始。
- Cohort 决策缓存只覆盖当前 `CohortManaged` 居民；不得把 Active Micro 重复计入同一批决策。

人口不变量相应为：

> `[INV-POP-v1.4] N_total = N_cohort_managed + N_active_micro`

### 3.5 同一动作规则与统一竞争队列

- Work、BuyWood、ChopWood、Start/ContinueRepair、Routine、Wait 的前置条件、效果、费用、持续时间和优先规则只定义一次。
- Oracle/Per-Agent 逐居民调用；Proposed 批量判定，但不得复制或另写一套含义不同的领域公式。
- 同一时刻竞争 Market、Forest、Repair Capacity 或其他稀缺资源的 CohortManaged 与 Active Micro 候选人，必须先进入同一个候选集合。
- 继续使用 v1.3 Phase 3 已冻结的去 ResidentID 偏差排序：`Seed + GameTime + ResidentID + ActionID` 生成稳定顺序，再取得唯一递增 ArriveID。
- Batch Event 可以在胜者确定后合并记录，但一个宏观批次不得绕过个人候选人、抢占 Active Micro，或让“被玩家观察”成为资源优先级。

## 4. 连续性、准确性与表现边界

### 4.1 连续性是零错误门

连续性检查只比较同一方法、同一居民在切换提交前后：

- 不可变身份完全相同；
- 切换瞬间 Money、Credit、Wood、HomeState 不改变；
- 已承诺事件保持相同 EventID、ReservationID、ArriveID 和剩余进度；
- 不发生 TaskReset、DuplicateCompletion、EventOwnerConflict、DuplicateTransaction、NegativeStock 或 Population/Wood residual。

零时间 `CohortManaged → ActiveMicro → CohortManaged` 往返必须逐字段一致。这不等于该居民 60 日后的结果必须逐项等于 Oracle。

### 4.2 准确性继续以 Oracle 为参考

Oracle 回答的是“Proposed 的批量近似与详细个体模拟相差多少”，继续使用既有宏观轨迹误差、政策效应误差、TVD、FirstAction 和硬错误，不新增或更换主要指标。

- 连续性失败：LOD 转换本身改名、改钱、重置或重复事件。
- 准确性误差：状态保存正确，但批量决策使后续行动或王国轨迹与 Oracle 不同。

两类结果必须分开报告，不得用“字段保存成功”替代准确性结论，也不得把正常近似误差误报为身份丢失。

### 4.3 望远镜与 UI（阶段 7 实现，本节只冻结边界）

- UI、姓名显示、相机缩放、Actor/动画开关均为只读表现，单独开关不得改变同 Seed 日志。
- 望远镜可在演示中提高 Criticality 并请求 LOD；请求提交必须原子化，且不能改变同一时刻已经存在的事实。
- LOD 切换后的未来行为允许与全程 CohortManaged 产生可测差异；正式方法比较只能使用固定 Activation Trace，不能让镜头选择实验样本。
- 对话只读 C++ CoreState，能验证居民“仍记得”已建模的姓名、身份、住宅、状态和事件；不得暗示已实现完整自然语言或长期社交记忆。

## 5. 方法公平性与性能风险

| 方法 | 个人状态 | 离屏计算边界 |
|---|---|---|
| Oracle | 200 人完整准确状态 | 全部运行详细 Utility+GOAP |
| Per-Agent | 全部独立 ID/状态 | 每游戏小时逐居民更新，不得 Cohort 聚合 |
| Simple | 王国总量和既有平均队列 | 不保留完整个人 CoreState |
| Proposed | 全部居民准确轻量 CoreState + 可重算 Cohort 缓存 | Cohort 批量决策，不运行离屏逐居民 GOAP |

v1.4 不预先宣称 Proposed 一定更快：

- 个人状态内存为 `O(N)`，不再宣称个体状态内存固定；
- Cohort 索引维护、候选分配和事务提交有自身成本；
- 若每小时完整扫描所有居民，部分 CPU 仍可能为 `O(N)`；
- 性能收益应主要来自减少 GOAP、逐居民决策调度、感知、Actor 和 Tick；
- 必须使用既有 `performance_1s.csv` 字段实测 2k/10k/20k，与公平实现的 Per-Agent 比较。若差异落在测量噪声内，论文必须如实报告。

不为证明性能而故意放慢 Per-Agent，也不在 Pilot 前增加新 Cohort 维度、压缩算法或未批准参数。

## 6. 阶段 0—3 返工范围

| 阶段 | 返工级别 | 必须修改 | 必须保留的回归 |
|---:|---|---|---|
| 0 | 小幅 | 所有居民生成稳定非零 PersistentID/Name；20 人文件改为正式测试样本而非能力池 | 人口构成、损伤分层、随机子流与确定性；Manifest/Hash 更新并记录 |
| 1 | 小幅或无 | 仅在需要时调整人口表示枚举/审计以支持 `CohortManaged XOR ActiveMicro` | Clock、Scheduler、Ledger、Reservation、Event Store 语义和 Phase 1 测试 |
| 2 | 中等偏大 | 将 Cohort 从个人资源真相改为可重算统计/批量决策缓存；接入共享动作规则与统一候选竞争 | 所有政策参数、公式、小时顺序、Stock/Flow 与既有 Phase 2 语义回归 |
| 3 | 中等 | 提取 Proposed/Oracle/Per-Agent 共用的 CoreState、动作前置条件/效果和竞争排序 | Utility/GOAP 语义、整数购买、信用优先付款、维修结算和 Oracle 行为回归 |

返工不得顺便实现阶段 5—8，也不得修改 Simple/Per-Agent 方法边界。旧 Digest 若仅因 Spec/Manifest 元数据变化而变化，必须注明原因；领域行为变化则视为回归失败，除非另获批准。

## 7. v1.4 返工与阶段 4 Definition of Done

### 7.1 Phase 0 补充 DoD

- 200、2k、10k、20k 的每个居民均有唯一稳定 ResidentID、非零 PersistentID 和确定性 Name。
- `PersistentID=ResidentID`；同配置与 Seed 两次 Manifest 逐字节一致。
- `persistent_test_pool.json` 仍恰好 20 人，Day 7/30 为同一 10 人，Day 45 为全部 20 人；其含义只为正式配对样本。
- 既有日志字段表不增加、不删除、不改名；更新后的 Config Hash 与三份输入文件 Hash 可重算。

### 7.2 Phase 2/3 返工 DoD

- Domain Rules 的每项冻结动作条件/效果只有一个定义；Oracle 与 Proposed 均引用它。
- Cohort 缓存可从 Canonical Resident Store、Ledger 和 Event Store 重建并逐项相同。
- 同时出现 Macro/Micro 稀缺资源请求时，进入同一稳定候选序列并产生唯一 ArriveID；观察状态不影响优先级。
- Phase 0—3 自动化测试全部通过；Phase 2/3 的政策、动作和 Oracle 语义无未批准变化。

### 7.3 Phase 4 DoD

- 固定测试池外任意 ResidentID 也能动态升级和降级；不存在运行期 Persistent 人数上限。
- 在 200 人自动化场景中，全部 200 人至少完成一次零时间往返；强连续字段逐项一致，Active Micro 始终 `≤50`。
- 固定 20 人继续执行 Day 7/30/45 正式 Trace；Phase 4 验证内存中的 CoreState 与转换记录，`npc_snapshots.csv` 和 `lod_transitions.jsonl` 的既有字段落盘由 Phase 6 Runner 验收。
- Repair 进行到 50% 时完成两次 LOD 切换：EventID、ReservationID、ArriveID、RemainingWork 连续，4 Wood 只结算一次。
- 至少一项 Market/Forest/Repair Capacity 竞争同时包含 CohortManaged 与 Active Micro 候选；同 Seed 的胜者和事务日志可复现。
- 零时间切换不改变权威事实；纯 UI/表现开关不改变日志留到 Phase 7 验收；切换后的未来近似差异进入后续准确性结果。
- 人口、Wood、Coin、Event Owner、Reservation 和幂等硬错误均为 0。
- 同 Seed 重跑的内存状态、事件、事务、竞争与转换记录产生相同确定性 Digest；落盘日志的逐字节一致性留到 Phase 6，CPU 时间不进入 Digest。

阶段 4 通过以上 DoD 前，不进入阶段 5。

### 7.4 v1.4 返工与 Phase 4 验收记录（2026-08-16）

- 实施分支：`phase-4-state-preserving-cohort`；UE 5.4 Development Editor 编译通过；NullRHI 全套 `AILODResearch` 自动化测试 14/14 Success，其中 Phase 4 为 6/6 Success。
- Phase 0：200、2k、10k、20k 全员稳定身份；固定 20 人继续只是连续性样本。`ConfigHash=7ECCEDAC87BF250D35DE738D53F0D709637804C5`。
- Phase 0 输入 SHA-256：Population=`31338B09B03884EA04984DEED4C18C3B27644B0D1E30B0BA959982708E1B9179`；Damage=`181AFE0282C3161AD5F014E0D9DF3E318B706D8524BB696EB2732E94809867D5`；Continuity Sample=`A375ABB6A78245F9EFBD2C9CFE0AC9CCB1057A5D10044D7CA21E79EE9B97D6FA`；RunA/RunB 逐项一致。v1.3 的旧 Hash 仅因 v1.4 Spec/Manifest 身份字段覆盖而失效。
- Phase 2：四场景王国终值、事务数量和政策语义与 v1.3 一致；Digest 因包含新的 ConfigHash 而变为 None=`5FB08CFC14EF3887BB651E7752A9C12C382ABCF7`、HarvestCap=`AB043680A7E318EC7B9CD4CE93630EBD53229263`、StateImport=`9EA0DD55F0C4691E1E176A580400BF55EFECA498`、RepairAid=`3CD4E7EBCBF6991400EF87526E9408877255D805`，不是领域行为变化。
- Phase 3：四场景 Digest 与 v1.3 逐项相同：None=`3FFFF68FF4CAAC749779F2707DB1CA778FBB27D9`、HarvestCap=`3EFC44178FC73EBB8AF30A082E622514C9DA269A`、StateImport=`0E9CA31A0C8ECF488949B41588A8C51B589B9C7C`、RepairAid=`70D7E19D8B175A14C6AF759890CAD18D9DA22DD6`，说明 Oracle 的冻结动作语义没有漂移。
- Phase 4 自动验收覆盖：全部 200 人逐字段零时间往返、Active Cap=50、样本外动态激活、Day 7/30 同一 10 人与 Day 45 全部 20 人、维修 50% 时 `Macro→Micro→Macro`、统一 Repair Capacity 竞争及真实 Ledger 提交、Cohort 全量重算核对和同 Seed Digest。
- 资源写入只经过 Ledger，CoreState 在事务后统一回读；Cohort 是可重算缓存；包括单一候选在内的 Repair 必须先进入统一竞争队列；旧 ArriveID 在同一未完成请求重新裁决或跨表示时不重发，且始终绑定原 `ResidentID + Action`；提交后标记为已消费，不得再次用于其他请求。
- 未实施且不得写成已完成：Phase 5 两个 Baseline、Phase 6 Runner/日志落盘/指标、Phase 7 地图/UI/望远镜/Actor、Phase 8 Pilot/正式性能数据。

## 8. v1.4 决策记录

| 版本 | 日期 | 决定 | 原因与影响 | 批准人 |
|---|---|---|---|---|
| v1.4 | 2026-08-16 | Proposed 为所有居民保存准确轻量 CoreState；Cohort 只聚合计算；固定 20 人降为正式连续性样本；任意居民可动态切换 | 避免随机解聚合破坏身份、资源和事件历史；代价是个人状态内存随人口增长，性能收益必须实测 | 项目作者 |
| v1.4 Consistency | 2026-08-16 | 共享动作规则、唯一权威来源和统一 Macro/Micro 竞争队列；连续性与 Oracle 准确性分开验证 | 防止多套行为模型、重复数据真相、观察者优势和错误实验归因 | 项目作者 |
