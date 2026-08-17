# AILOD MVP 当前有效规则索引

**索引版本：1.7**<br>
**日期：2026-08-17**<br>
**用途：说明当前规则的读取顺序、冲突优先级和各文档职责。**<br>
**性质：本文件只做导航，不新增模型规则、不替代原规格，也不构成阶段验收。**

## 1. 当前规则怎样读取

当前完整规则集不是某一个文件，而是以下文件按顺序叠加后的结果：

1. `AILOD_MVP_Prototype_Implementation_Spec_CN.md`（v1.1 基准规格）；
2. `AILOD_MVP_Prototype_Phase_Acceptance_Spec_CN_v1.2.md`；
3. `AILOD_MVP_Prototype_Phase_Acceptance_Spec_CN_v1.3.md`；
4. `AILOD_MVP_Prototype_Phase_Acceptance_Spec_CN_v1.4.md`；
5. `AILOD_MVP_Prototype_Phase_Acceptance_Spec_CN_v1.5.md`；
6. `AILOD_MVP_Prototype_Phase_Acceptance_Spec_CN_v1.6.md`。

冲突处理规则：

- 后一版本只覆盖其中明确写出的冲突项；
- 没有被后续版本明确覆盖的内容，继续继承较早版本；
- v1.6 是当前最高版本，但不能脱离 v1.1—v1.5 单独阅读；
- v1.1 中“单一事实源”的表述应理解为“基准事实源”。当前完整事实源是 v1.1 加 v1.2—v1.6 的累计覆盖链；
- 不得根据摘要、旧状态表或旧交接说明，反向覆盖正式规格中的较新规则。

## 2. 每份文档负责什么

| 文档 | 当前职责 | 使用边界 |
|---|---|---|
| v1.1 Implementation Spec | 研究问题、系统边界、政策与场景、公式、时间线、实验矩阵、指标、日志 Schema、阶段 0—8 基础 DoD | 是基准规格，但冲突项必须应用 v1.2—v1.6 覆盖 |
| v1.2 Acceptance Spec | 整数初值与付款、Repair Aid 资格、ArriveID、损伤清单、正式压力场景、性能环境等已批准修正 | 只覆盖明确列出的 v1.1 条目 |
| v1.3 Acceptance Spec | 第一版 MVP 范围、四方法验证矩阵、数据契约以及 Phase 0—3 验收记录 | 研究范围继续有效；20 人 Persistent 上限等条目已被 v1.4 覆盖 |
| v1.4 Architecture Spec | 全员轻量 CoreState、状态与计算分离、唯一权威来源、统一动作和竞争、Phase 4 验收 | 是当前 Proposed 连续性架构的基础 |
| v1.5 Unified Runtime Spec | 单一权威 Runtime、四方法共享语义、统一小时管线、Phase 5 入口与 DoD | 精确 Cohort 一致性要求已被 v1.6 覆盖 |
| v1.6 Phase 5.1 Spec | 受控 Cohort 近似、固定 Activation Trace、Simple 激活公平性、失败原子边界、运行模式和 Phase 6 前置架构债务 | 是当前 Phase 5.1 的最高优先级规格 |
| Phase 5 Handoff v1.0 | Phase 5 开始前的导航、源码位置、历史现场和验证方法 | 生成时间早于 v1.6；不是模型事实源，涉及当前 Phase 5.1 时必须回到 v1.6 和检查点核对 |
| Phase 5.1 Checkpoint | Phase 5.1 实现证据、Hash、Digest、自动验收结果、已知边界和待完成项 | 是验收记录，不新增或覆盖模型规则 |
| Phase 6 Incremental Plan | 将既有 Phase 6 范围拆成 6A—6F 检查点并记录逐步验收状态 | 是实施导航与检查点，不新增或覆盖模型规则；每一步必须经作者确认后才能进入下一步 |

## 3. 当前最容易误读的有效规则

| 主题 | 当前有效规则 | 权威出处 |
|---|---|---|
| Proposed 的人口表示 | 每名居民永久保留准确轻量 CoreState；Cohort 聚合的是离屏决策计算，不是把居民本人删除 | v1.4 §2、§3 |
| LOD 状态 | Proposed 使用 `CohortManaged XOR ActiveMicro`；Persistent 不是第三套 AI 或有限名额 | v1.4 §2 |
| 可激活居民 | Proposed 任意居民都可动态激活；固定 20 人只是正式连续性样本；全局 Active Micro Cap 仍为 50 | v1.4 §2、§4 |
| Cohort 近似 | 使用 v1.6 的粗粒度 Key、Cohort 均值代表和确定性成员分配；不得恢复 v1.5 的精确事实分组作为正式 Proposed | v1.6 §3 |
| 近似分歧 | `CohortDecisionDisagreementCount` 和 `CohortAllocationFallbackCount` 是研究误差信号，不是必须清零的硬错误 | v1.6 §3.3 |
| 个人提交 | 每名居民仍以真实 CoreState、Ledger、王国资源和统一竞争重新验证；成功后保留独立 Event/Reservation/ArriveID | v1.6 §3.2、§5 |
| Simple Aggregate | 离屏只保存总量/均值；激活时必须真实重建临时 Micro、运行共享个人行为并写回，窗口结束后删除临时 CoreState | v1.6 §4 |
| 正式 Activation Trace | Day 7—8 固定 10 人；Day 14—15 分层 20 人；Day 30—31 同一 10 人；Day 45—46 固定 20 人 | v1.6 §4 |
| 动作语义 | Work、BuyWood、ChopWood、Repair、Routine、Wait 只由共享 Individual Domain 定义一次 | v1.4 §3.5；v1.5 §3.3；v1.6 §5.1 |
| 提交失败 | 写入前完成 Preflight；被拒绝动作不得留下 Ledger、Event、Scheduler、Reservation 或额度残留 | v1.6 §5.2 |
| 时间观察点 | 每小时按“结算 T → 政策/Flow/价格 → 规划/提交 → 推进并结算 T+1 → Audit → T+1 Snapshot”执行 | v1.6 §6 |
| 正式实验政策 | 正式比较使用固定政策输入；动态 King Utility 不属于当前 MVP | v1.3 §2、§4；v1.5 §1 |
| 性能结论 | Validation 复算、完整 Audit、Snapshot 和日志成本必须与生产算法成本分开；Phase 5.1 不能宣称 Proposed 已更快 | v1.6 §6；Phase 5.1 Checkpoint §7 |

## 4. 当前阶段边界

截至本索引日期，Phase 5.1 的实现和自动验收已经通过，项目作者已于 2026-08-17 确认检查点：

> 本检查点随 Phase 5.1 封板提交，未推送。

这是一条状态记录，不等于模型规则。该确认允许 Phase 5.1 封板并在独立分支开始 Phase 6；它不等于已经取得正式性能或准确性结论。

进入 Phase 6 后必须处理：

- 最小 `ISimulationBackend`；
- `Initialize / StepHour / Finalize` 生产会话；
- 只读 Observer/Event Sink；
- 正式数据只走统一生产会话；
- 在证据支持后才决定是否优化完整审计的账户查询。

以上边界以 v1.6 §8 和 Phase 5.1 Checkpoint §8 为准。

当前位于 `phase-6-backend-observer-logs`。Phase 6A、6B、6C 已分别以本地提交 `71e3565`、`c0e84d2`、`eb44bf3` 封板；项目作者已于 2026-08-17 确认 Phase 6D 检查点，并授权 6D 封板后在独立分支开始 6E。所有提交仍不推送。

## 5. 后续修改规则

- 不回写历史版本来伪装规则从未变化；
- 新冲突必须先讨论，再用新的版本化覆盖文档记录；
- 新覆盖必须写明旧条目、替代规则、原因、影响和批准人；
- 阶段状态、Git 现场和测试结果写入检查点，不写成本索引中的永久模型规则；
- 开始任何后续实现前，先检查本索引、当前 Git 状态、对应阶段的最高版本规格和检查点。
