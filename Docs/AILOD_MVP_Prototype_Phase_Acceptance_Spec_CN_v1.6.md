# Hierarchical AI / Simulation LOD：MVP v1.6 Phase 5.1 架构修正规格

**版本：v1.6**<br>
**日期：2026-08-16**<br>
**状态：研究方向已由项目作者批准；Phase 5.1 实现与自动验收通过；检查点已于 2026-08-17 由项目作者确认**<br>
**基准文档：`AILOD_MVP_Prototype_Implementation_Spec_CN.md` v1.1**<br>
**前序修订：v1.2、v1.3、v1.4、v1.5**<br>
**用途：修正 Phase 5 对抗复核发现的构念效度、激活公平性、动作失败边界和测量污染问题。历史文档保留不改。**

## 1. 文档优先级与不变项

实现依次读取 v1.1、v1.2、v1.3、v1.4、v1.5 和本文件。本文件只覆盖明确写出的冲突；未覆盖内容继续有效。

- 不改变冻结的人口规模、场景、政策、公式、小时步长、Seeds、Runs、Active Micro Cap、主要指标和日志字段。
- 不加入玩家任务、跨国运输、Food、动态国王、LLM、PCG、完整社交或额外经济复杂度。
- 保留 v1.4 的全员轻量 CoreState、单一权威 Ledger/Event、每居民独立承诺事件和统一竞争原则。
- Phase 5.1 通过本文件 DoD 前不得进入 Phase 6。
- `SchemaVersion` 保持 `1.1`；本次批准的模型变化使 `SpecVersion` 更新为 `1.6`，必须重建并记录 ConfigHash、输入 Hash 和 Phase 5 回归 Digest。

## 2. 对抗复核结论与批准选择

v1.5 Proposed 使用 `Kingdom + Profession + IncomeBand + Cash + RepairCredit + Wood + HomeState` 精确分组。该 Key 已包含当前 GOAP 首行动所需的全部个体事实，因此组内成员必然与代表产生相同计划。固定 Seed 下 Proposed 与 Oracle/Per-Agent 的事务数和事件数完全相同；它证明的是行为保持的重复计算消除，不能单独支撑“有可测准确性—性能权衡的 Simulation LOD”主张。

项目作者已批准以下替代方向：

> Proposed 永久保留每名居民的轻量 CoreState 和独立承诺事件，但 CohortManaged 使用有意降精度的 Cohort 统计来生成行动数量，再确定性分配到居民；个人提交仍验证真实资源和前置条件。由此产生的差异作为准确性误差测量，不再作为实现错误清零。

这不是为了人为制造差异，而是恢复本研究原本要评估的自变量：以较低的离屏决策精度换取计算收益，同时保持身份、资源、事件和 LOD 连续性。

## 3. Proposed 受控 Cohort 近似 `[APPROVED OVERRIDE]`

### 3.1 决策 Cohort Key

CohortManaged 居民按以下低维 Key 建立可重算缓存：

> `Kingdom × Profession × IncomeBand × HomeState × MacroIntent × PurchasingPowerBand × WoodBand`

其中：

- `PurchasingPower = Cash + RepairCredit`；Band 固定为 `0–3 / 4–7 / 8+ coin`；
- `WoodBand` 固定为 `0 / 1–3 / 4+ wood`；
- 不把精确 Cash、RepairCredit、Wood、ResidentID、Name 或 Representation 加入 Key；
- Active Micro 居民不进入 CohortManaged 决策缓存。

这些 Band 只用于 Phase 5.1 的 Cohort 决策近似，不改变 Ledger 中的准确个人余额，也不增加正式实验的新可调参数。若 Pilot 以后需要改 Band，必须按 `[PILOT]` 变更流程记录，不能根据正式结果调整。

### 3.2 Cohort 计划与确定性分配

每个非空决策 Cohort 每小时执行一次：

1. 从成员的权威 CoreState/Ledger 视图计算 `PopulationCount、CashSum、RepairCreditSum、WoodSum`；
2. 用整数四舍五入后的 Cohort 均值构造不带身份的合成代表状态；
3. 使用共享 Utility/GOAP Domain 只计算一次首行动；
4. 将该行动形成 `PopulationCount` 个候选，并按 `Seed + GameTime + ResidentID + ActionID` 的固定顺序分配给成员；
5. 每名居民进入真实王国/资源/时间窗口的统一竞争；
6. 共享动作前置条件以居民真实 CoreState 和 Ledger 重新验证；不合法或资源竞争失败时执行 Wait，不得借用 Cohort 均值、他人资金或他人木材；
7. 成功者仍创建独立 EventID、Owner、ReservationID、ArriveID 和进度。

“Cohort 计划与逐居民 GOAP 不一致”改为被测近似信号：记录 `CohortDecisionDisagreementCount` 与 `CohortAllocationFallbackCount`，不得把它们加入硬错误门。身份改变、事件重置、资源残差、重复事务和非法提交仍必须为 0。

### 3.3 覆盖 v1.5 的精确一致性 DoD

v1.5 第 7 节“每个 Cohort 的代表计划必须与组内每名居民逐人计划一致”由以下要求替代：

- Validation 模式可以逐成员复算，只用于记录近似分歧；
- 同 Seed 的 Cohort Key、合成代表、行动数量、成员分配和最终结果必须确定性一致；
- 所有实际提交必须满足个人前置条件、真实资源约束和统一竞争顺序；
- Phase 6/8 使用既有 TVD、轨迹误差与政策效应误差评价近似，不以“必须等于 Oracle”为通过条件。

## 4. 固定 Activation Trace 与 Simple 公平性 `[APPROVED OVERRIDE]`

正式 Trace 完整恢复为：

| 时间 | 激活 | 结束 |
|---|---:|---:|
| Day 7 | 固定连续性样本 10 人 | Day 8 |
| Day 14 | 排除固定连续性样本后，A/B 各 10 人，并按 `Profession × IncomeBand` 以 `1/1/6/2` 分层选择，共 20 人 | Day 15 |
| Day 30 | Day 7 的同一 10 人 | Day 31 |
| Day 45 | 固定连续性样本全部 20 人 | Day 46 |

四方法共享完全相同的 ResidentID 与时间表。Oracle/Per-Agent 虽然全部居民本来就逐人计算，也必须记录相同激活观察点，保证连续性和 FirstAction 样本一致。

Simple 继续禁止保存离屏个人 CoreState，但激活不能只改人数标签：

1. 激活时从王国可拆分的 HomeState 总量和 Cash/Credit/Wood 均值确定性重建临时 Active Micro 状态；已属于 aggregate delay entry 的 Busy/UnderRepair 参与者不会被重复抽出；
2. 临时状态只在激活窗口存在，资源从 Simple 总量账户转入临时账户，HomeState 从 aggregate count 移出；
3. 激活期间使用与其他方法相同的个人 Utility/GOAP、动作时长、Ledger、Event 和 Scheduler；
4. 降级时把个人余额和 HomeState 写回 Simple 总量；未完成承诺事件保留 EventID、ArriveID、ReservationID 和剩余结束时间，但转换为不带个人状态的 aggregate delay entry；
5. 写回后删除临时 CoreState；再次激活从当时总量重新构造，不保留离屏个人历史；
6. 记录重建数、写回数、FirstAction 数和窗口结束后的临时 CoreState 数。

该规则让 Simple 承担相同 Active Micro 工作，同时保持其“离屏只保存总量/均值”的能力下限。

## 5. 动作定义、提交与失败边界

### 5.1 单一动作规则

Work、BuyWood、ChopWood、StartRepair、ContinueRepair、Routine 和 Wait 的前置条件、持续时间、费用、收入和状态效果由共享 Individual Domain 解析。GOAP 搜索、个人提交和 Simple 的临时 Active Micro 不得各写含义不同的数值。

Simple aggregate 可以对参与人数做均值近似，但动作时长、每户维修耗木、付款取整、收入和完成效果仍读取共享定义。

### 5.2 MVP 原子提交边界

每个动作分成两段：

1. **Preflight：** 在任何写入前验证居民状态、余额、王国资源、容量、Event/Reservation 参数和 Scheduler 时间；测试故障也只能在此边界拒绝；
2. **Commit：** 使用预验证结果创建 Event/Reservation、提交 Ledger 并加入 Scheduler。单线程权威运行时在两段之间不得推进时间或处理其他候选，因此提交后的逻辑步骤视为不可失败不变量。

BuyWood、ChopWood、StartRepair 和 StateImport 各提供一次性故障注入测试。被拒绝动作前后比较 Ledger 事务数、Event 数、Scheduler pending、Active Reservation、居民事件引用和相关额度；任何变化计入 `RejectedActionResidueCount` 并使硬错误门失败。

`TaskResetCount` 必须由激活/降级前后的 EventID、ArriveID、ReservationID、Start/EndTime 实际比较产生，不能只声明字段。FirstAction 必须由每次激活实例实际记录。

## 6. 时间、审计与测量边界

每小时统一定义为：

> 时刻 T 的到期事件已结算 → 政策/Stock Flow/价格 → 规划、竞争与提交 → 顺序推进并结算到 T+1 → Audit → 标记为 T+1 的 Snapshot

因此同一时间戳的 Audit 与 Snapshot 观察同一权威状态。D60 仍只结算既有 `ExecuteAt <= D60T00:00` 事件，不生成 D60 新计划。

运行模式固定分离：

| 模式 | 用途 | 额外工作 |
|---|---|---|
| Validation | 开发与检查点 | 逐小时完整 Audit；可开启逐成员近似复算和 Snapshot |
| Accuracy | Phase 6/8 准确性 | 保留研究日志与逐小时正确性门；不默认执行逐成员 GOAP 复算 |
| Performance | Phase 8 性能 | 禁止逐成员近似复算和完整 NPC 日志；逐小时只使用增量错误计数，Run 结束执行完整 Audit |

诊断必须分开记录生产规划次数与 Validation 复算次数，并单列 Audit/Snapshot 的居民访问量。正式性能结论不得把 Validation 复算、完整审计或落盘日志成本伪装成 Cohort 算法成本。

## 7. Phase 5.1 Definition of Done

- 修改前 v1.5 Development Editor 构建和 18 项自动化基线已现场复验；修改后 Phase 0–5 全部回归继续通过。
- Proposed 使用第 3 节粗粒度决策 Cohort；固定测试至少出现一个可复现的近似分歧，且所有实际提交合法、硬错误为 0。
- Day 14/15 的 20 人分层激活存在；四方法共享完整 60 次激活实例，Active Micro 始终不超过 50。
- Simple 每个激活窗口真实创建临时 Micro 状态、运行个人规划并写回；最终临时 CoreState 为 0，资源和人口守恒。
- 每次激活都有 FirstAction 或已有承诺动作记录；TaskReset 由真实比较产生且为 0。
- Buy、Chop、Repair、Import 四类故障注入均被触发一次，`RejectedActionResidueCount=0`。
- Audit 与 Snapshot 时间语义一致；Validation/Accuracy/Performance 模式和诊断成本边界可自动验证。
- Oracle 仍拒绝 200 人以上；Simple、Per-Agent、Proposed 的 2k/10k/20k StateImport 全时段冒烟继续通过。
- 更新 Phase 5 检查点，记录 v1.6 Hash、Digest、近似分歧、激活、故障和测试结果；不宣称已有正式性能或准确性结论。

## 8. 有意推迟但必须在 Phase 6 前处理的架构债务

- 将当前统一 Runtime 的方法分支拆成最小 `ISimulationBackend`，但不复制 Scenario、Action、Ledger 或 Scheduler。
- 把阻塞式 `Run()` 包装为 `Initialize / StepHour / Finalize` 会话，并提供只读 Observer/Event Sink，供 300 秒实时采样和 UE Tick 接入。
- Phase 6 只允许调用统一生产会话；旧 Phase 2/3 Runner 标记为历史特征回归，不得生成正式 Proposed 数据。
- 完整审计的字符串账户查询可在证据确认后改为稳定账户句柄或增量总量；该优化不得改变 Ledger 事实或日志 Schema。

这些项目不在 Phase 5.1 中提前实现，以避免把研究模型修正扩大成 Phase 6 Runner 重构；但 Phase 6 DoD 必须显式覆盖。

## 9. v1.6 决策记录

| 版本 | 日期 | 决定 | 原因与影响 | 批准人 |
|---|---|---|---|---|
| v1.6 Controlled Approximation | 2026-08-16 | Proposed 从精确等价分组改为粗 Band + Cohort 均值合成代表 + 确定性个人分配 | 恢复可测准确性—成本权衡；分歧成为研究误差，硬连续性仍为零错误门 | 项目作者 |
| v1.6 Activation Fairness | 2026-08-16 | 恢复 Day 14/15 分层 20 人，并要求 Simple 临时 Micro 重建、个人运行和总量写回 | 让四方法承担相同激活工作，补测 Simple 的能力下限 | 项目作者 |
| v1.6 Engineering Gates | 2026-08-16 | 增加动作 Preflight、四类故障注入、真实 TaskReset/FirstAction、统一 Audit/Snapshot 时点和三种运行模式 | 消除空计数、半提交风险与性能测量污染 | 项目作者 |
