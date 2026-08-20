# AILOD MVP 当前有效规则索引

**索引版本：3.9**<br>
**日期：2026-08-20**<br>
**用途：说明当前规则的读取顺序、冲突优先级和各文档职责。**<br>
**性质：本文件只做导航，不新增模型规则、不替代原规格，也不构成阶段验收。**

## 1. 当前规则怎样读取

当前完整规则集不是某一个文件，而是以下文件按顺序叠加后的结果：

1. `AILOD_MVP_Prototype_Implementation_Spec_CN.md`（v1.1 基准规格）；
2. `AILOD_MVP_Prototype_Phase_Acceptance_Spec_CN_v1.2.md`；
3. `AILOD_MVP_Prototype_Phase_Acceptance_Spec_CN_v1.3.md`；
4. `AILOD_MVP_Prototype_Phase_Acceptance_Spec_CN_v1.4.md`；
5. `AILOD_MVP_Prototype_Phase_Acceptance_Spec_CN_v1.5.md`；
6. `AILOD_MVP_Prototype_Phase_Acceptance_Spec_CN_v1.6.md`；
7. `AILOD_MVP_Prototype_Phase_Acceptance_Spec_CN_v1.7.md`。

冲突处理规则：

- 后一版本只覆盖其中明确写出的冲突项；
- 没有被后续版本明确覆盖的内容，继续继承较早版本；
- v1.7 是当前最高版本，但不能脱离 v1.1—v1.6 单独阅读；
- v1.1 中“单一事实源”的表述应理解为“基准事实源”。当前完整事实源是 v1.1 加 v1.2—v1.7 的累计覆盖链；
- 不得根据摘要、旧状态表或旧交接说明，反向覆盖正式规格中的较新规则。

## 2. 每份文档负责什么

| 文档 | 当前职责 | 使用边界 |
|---|---|---|
| v1.1 Implementation Spec | 研究问题、系统边界、政策与场景、公式、时间线、实验矩阵、指标、日志 Schema、阶段 0—8 基础 DoD | 是基准规格，但冲突项必须应用 v1.2—v1.7 覆盖 |
| v1.2 Acceptance Spec | 整数初值与付款、Repair Aid 资格、ArriveID、损伤清单、正式压力场景、性能环境等已批准修正 | 只覆盖明确列出的 v1.1 条目 |
| v1.3 Acceptance Spec | 第一版 MVP 范围、四方法验证矩阵、数据契约以及 Phase 0—3 验收记录 | 研究范围继续有效；20 人 Persistent 上限等条目已被 v1.4 覆盖 |
| v1.4 Architecture Spec | 全员轻量 CoreState、状态与计算分离、唯一权威来源、统一动作和竞争、Phase 4 验收 | 是 v1.6 Current Proposed 的历史架构基础；全员动态状态和个人竞争已被 v1.7 明确覆盖 |
| v1.5 Unified Runtime Spec | 单一权威 Runtime、四方法共享语义、统一小时管线、Phase 5 入口与 DoD | 精确 Cohort 一致性要求已被 v1.6 覆盖 |
| v1.6 Phase 5.1 Spec | 受控 Cohort 近似、固定 Activation Trace、Simple 激活公平性、失败原子边界、运行模式和 Phase 6 前置架构债务 | 固定 Trace、Simple 公平性、动作定义、小时顺序和测量隔离继续有效；Proposed 的人口表示、个人候选/提交和旧日志语义已被 v1.7 覆盖 |
| v1.7 Cohort Batch Spec | Identity Registry、权威 Joint State、Action Flow、Batch Claim/Event、聚合 Ledger、Capsule、Dynamic Lift/Restrict、新 Digest/Schema 和 6G-B0—B5 | 是 6G-B 及最终 Proposed 的最高优先级规则；B1/B2 仍由 v1.6 运行，B3 才一次性切换 Macro 权威，B4 接入完整动态 LOD；各检查点必须标明 authority mode，不得长期双写或形成混合真相 |
| Phase 5 Handoff v1.0 | Phase 5 开始前的导航、源码位置、历史现场和验证方法 | 生成时间早于 v1.6；不是模型事实源，涉及当前 Phase 5.1 时必须回到 v1.6 和检查点核对 |
| Phase 5.1 Checkpoint | Phase 5.1 实现证据、Hash、Digest、自动验收结果、已知边界和待完成项 | 是验收记录，不新增或覆盖模型规则 |
| Phase 6 Incremental Plan | 将既有 Phase 6 范围拆成 6A—6F 检查点并记录逐步验收状态 | 是实施导航与检查点，不新增或覆盖模型规则；每一步必须经作者确认后才能进入下一步 |
| Phase 6 Checkpoint | Phase 6A—6F 的实验基础设施、测量边界、回放/重建证据、工程成本分解和待确认状态 | 是总验收记录，不新增或覆盖模型规则；工程冒烟不得当作正式研究结论 |
| Phase 6G-A Checkpoint | Proposed Macro 子阶段定向计时、计数、Digest 回归和瓶颈边界 | 是工程诊断记录，不新增模型规则，也不是正式性能实验或论文结论 |
| Phase 6G-B0 Checkpoint | v1.7 规则冻结、旧规则冲突审计、B0—B5 实施门和未完成项 | 是规则阶段验收记录，不代表 Batch/Lift 代码已实现 |
| Phase 6G-B1 Checkpoint | v1.7 Shadow Identity/Joint State/Action Flow/Claim 的三方对账、旧 Digest 回归和工程诊断成本 | 是只读旁路验收记录；v1.6 仍是权威，不能把 B1 当作已获得 Batch 性能或正式实验资格 |
| Phase 6G-B2A Checkpoint | Wait/Routine 用一条事件代表整批人数、到期归还、错误拒绝和重复运行证据 | 是隔离原型验收记录；未接入完整 Proposed，不代表 Work/Ledger、资源竞争、Dynamic LOD 或规模性能已经完成 |
| Phase 6G-B2B Checkpoint | Work 整批工资、整数 Coin、到期结算、防重复付款、失败恢复和国库不变证据 | 是隔离原型验收记录；未接入完整 Proposed，不代表有限资源竞争、完整 Cohort 迁移或规模性能已经完成 |
| Phase 6G-B3 Checkpoint | 六种共享离屏行动、Macro 与 Active 单人统一竞争、Market/Forest/Repair 批量预约和结算、失败恢复与权威 Macro 会话证据 | 是无动态玩家轨迹的 v1.7 权威 Macro 验收记录；尚未实现 B4 的任意居民 Lift/Restrict、Capsule、正式 Trace，也不代表 B5 性能和准确性已通过 |
| Phase 6G-B4 Checkpoint | 任意居民按身份恢复、玩家记忆、进入/退出群体、进行中任务和木材预订拆分/合并、固定轨迹、Active=50 与失败恢复证据 | 是隔离 v1.7 权威会话的动态 LOD 验收记录；尚未接入完整 Experiment Runner，也不代表 B5 的准确性、性能、50k/100k 压力和正式实验资格已通过 |
| Phase 6G-B5A Checkpoint | 新旧 Proposed 明确选择、v1.7 完整时钟工程冒烟、Schema 1.2 清单和清单重放 | 只证明新运行器接线正确；目前只开放无政策 Performance 工程冒烟，不代表政策行为、200 Oracle 准确性或 2k—100k 性能已经通过 |
| Phase 6G-B5B Checkpoint | 完整共享行为、地震、三种固定政策、Schema 1.2 明细日志和 200 人 Oracle 工程对照 | 证明完整准确性数据链已接通并能报告近似误差；只是单 Seed 工程检查，不代表 2k—100k 规模、3 倍目标或正式统计已经通过 |
| Phase 6G-B5C Checkpoint | 2k、10k、20k 的确定性重放、硬错误、批量增长、生产总成本和离线重建 | 证明中等规模工程链路与整场成本口径已接通；单轮 20k 速度不能替代 B5E 的重复和顺序控制，也不代表 50k/100k 已通过 |
| Phase 6G-B5D-Lite Checkpoint | Proposed-only 的 50k/100k 压力运行、100k 重放和 20k→100k 后台增长检查 | 证明当前工程环境能推进 100k，且后台对象未随人口增长；不提供 50k/100k Per-Agent 速度比，也不替代 B5E |

## 3. 当前最容易误读的有效规则

| 主题 | 当前有效规则 | 权威出处 |
|---|---|---|
| Proposed 的人口表示 | 全员永久保存静态 Identity；未激活人口的当前动态事实由 Cohort Joint State 权威表示，Active 才有完整个人状态，Capsule 只稀疏保存观察与谱系条件 | v1.7 §2—§4、§9 |
| LOD 与 Capsule | 人口始终满足 `Σ JointCell.Count + Active = N`；Capsule 是可选记忆覆盖，不额外计人口、资源或第三套 AI | v1.7 §3、§9 |
| 可激活居民 | Proposed 任意 ResidentID 均可按需 Lift；固定 20 人仍是正式连续性样本；全局 Active Micro Cap 仍为 50 | v1.6 §4；v1.7 §1、§10 |
| Cohort 表示 | 外层 Key 固定为 `Kingdom × Profession × IncomeBand`；内层使用 HomeState、Commitment、PurchasingPowerBand、WoodBand、AidEligibility 的稀疏联合格子 | v1.7 §5 |
| Cohort 决策 | 每个非空可决策 Joint Cell 最多规划一次并形成整数 Action Flow；禁止按 ParticipantCount 展开个人候选 | v1.7 §6 |
| 提交与竞争 | 匿名离屏行动使用 Batch Claim/Event 和聚合 Ledger；Macro Batch 与 Active `Count=1` 进入同一资源 Scope，使用确定性整数配额 | v1.7 §7—§8 |
| 近似分歧 | Cohort/Oracle 行动与轨迹差异、Lift 重建和 FirstAction 差异是研究误差信号；人口/资源/Batch/Split-Merge/Capsule 残差仍是零错误门 | v1.7 §13 |
| Simple Aggregate | 离屏只保存总量/均值；激活时必须真实重建临时 Micro、运行共享个人行为并写回，窗口结束后删除临时 CoreState | v1.6 §4 |
| 正式 Activation Trace | Day 7—8 固定 10 人；Day 14—15 分层 20 人；Day 30—31 同一 10 人；Day 45—46 固定 20 人 | v1.6 §4 |
| 动作语义 | Work、BuyWood、ChopWood、Repair、Routine、Wait 只由共享 Individual Domain 定义一次 | v1.4 §3.5；v1.5 §3.3；v1.6 §5.1 |
| 提交失败 | 写入前完成 Preflight；被拒绝动作不得留下 Ledger、Event、Scheduler、Reservation 或额度残留 | v1.6 §5.2 |
| Batch 失败 | Batch Preflight 后一次性提交 Count、聚合 Ledger、Batch Event/Reservation 和 Scheduler；失败不得留下部分 Flow 或 ParticipantRef | v1.7 §8 |
| Lift/Restrict | 人口、资源、事件谱系和 Capsule 必须原子迁移；零时间往返总量与谱系不变，离屏经过时间后的个人当前状态允许受控近似 | v1.7 §10 |
| 时间观察点 | 每小时按“结算 T → 政策/Flow/价格 → 规划/提交 → 推进并结算 T+1 → Audit → T+1 Snapshot”执行 | v1.6 §6 |
| 正式实验政策 | 正式比较使用固定政策输入；动态 King Utility 不属于当前 MVP | v1.3 §2、§4；v1.5 §1 |
| 性能结论 | Validation 复算、完整 Audit、Snapshot 和日志成本必须与生产算法成本分开；整场速度使用所有 `ai_cpu_ms` 的总和，约一秒桶的 Mean/P95 不能代替总速度比 | v1.6 §6；v1.7 §13.3；Phase 5.1 Checkpoint §7 |
| 复杂度边界 | 一次性 Identity 初始化/静态内存允许 `O(N)`；每小时不得扫描全员或生成 `O(N)` 候选、事件、事务；ParticipantCount 作为整数随人口增长不等于 Batch 对象数增长 | v1.7 §1、§3、§13 |
| 版本与 Digest | B1 Shadow 仍以 v1.6/Schema 1.1 为权威；v1.7 权威接管时使用 Spec 1.7、Schema 1.2、新 ConfigHash 和新 Digest | v1.7 §1、§11 |

## 4. 当前阶段边界

截至本索引日期，Phase 5.1 的实现和自动验收已经通过，项目作者已于 2026-08-17 确认检查点：

> 本检查点随 Phase 5.1 封板提交，未推送。

这是一条状态记录，不等于模型规则。该确认允许 Phase 5.1 封板并在独立分支开始 Phase 6；它不等于已经取得正式性能或准确性结论。

Phase 6A—6F 已经实现以下内容：

- 最小 `ISimulationBackend`；
- `Initialize / StepHour / Finalize` 生产会话；
- 只读 Observer/Event Sink；
- 正式数据只走统一生产会话；
- 生产、Validation、Audit、Snapshot、Observer、序列化与文件写入成本隔离；
- Accuracy 与 Performance 原始文件的离线汇总重建。

以上 Phase 6A—6F 历史边界以 v1.6 §8 和 Phase 5.1 Checkpoint §8 为准；6G-B 的新 Proposed 规则以 v1.7 为准。

当前位于 `phase-6g-b-cohort-batch`。Phase 6A—6F 已分别以本地提交 `71e3565`、`c0e84d2`、`eb44bf3`、`93282ed`、`6ee6873`、`c629bb6` 封板；6G-A、6G-B0、6G-B1、6G-B2A、6G-B2B、6G-B3、B4、B5A、B5B 和 B5C 已分别以本地提交 `41d3bae`、`8a1ac11`、`90020f2`、`af3b253`、`37120c4`、`0ed8c16`、`97e5843`、`8f7adbb`、`e17d0dc`、`b0d75e0` 封板。B5D-Lite 已完成 Proposed-only 的 50k/100k 压力运行、100k 重放和 37 项完整回归；两档硬错误为 0，后台状态格、Batch 对象和 ResidentTouches 与 20k 相同。项目作者已确认 B5D-Lite，正在独立本地提交封板；B5E 的 20k 重复、顺序控制和 3 倍目标决定将在提交后开始。Phase 7 和 Phase 8 尚未开始，所有既有提交均未推送。

## 5. 后续修改规则

- 不回写历史版本来伪装规则从未变化；
- 新冲突必须先讨论，再用新的版本化覆盖文档记录；
- 新覆盖必须写明旧条目、替代规则、原因、影响和批准人；
- 阶段状态、Git 现场和测试结果写入检查点，不写成本索引中的永久模型规则；
- 按 v1.7 §14，所有新验收文档必须先用大白话解释，再补充必要的技术名称；不能假设项目作者预先懂术语，并且必须说明每项证据“能证明什么、不能证明什么”；
- 开始任何后续实现前，先检查本索引、当前 Git 状态、对应阶段的最高版本规格和检查点。
