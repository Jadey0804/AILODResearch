# AILOD MVP 当前有效规则索引

**索引版本：5.1**<br>
**日期：2026-08-23**<br>
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
7. `AILOD_MVP_Prototype_Phase_Acceptance_Spec_CN_v1.7.md`；
8. `AILOD_MVP_Prototype_Phase_Acceptance_Spec_CN_v1.8.md`；
9. `AILOD_MVP_Prototype_Phase_Acceptance_Spec_CN_v1.9.md`；
10. `AILOD_MVP_Prototype_Phase_Acceptance_Spec_CN_v2.0.md`（只覆盖可视化/互动 Demo 协议，不升级模型行为）。

冲突处理规则：

- 后一版本只覆盖其中明确写出的冲突项；
- 没有被后续版本明确覆盖的内容，继续继承较早版本；
- v1.9 仍是当前 `[FROZEN]` 模型最高版本，只覆盖每栋房的状态连续性、批量维修到具体 HomeID 的对应、Lift 读取顺序和住房承诺计时；其余 v1.7 模拟行为与 v1.8 加固规则仍然有效；
- v2.0 是当前最高的可视化/互动 Demo 协议，只规定模式隔离、只读接口、空间、代理、Actor、PCG、地图、UI 和验收顺序；它不得覆盖 v1.9 的领域模型，也不得改变 `1.9-domain-v1`；
- v1.1 中“单一事实源”的表述应理解为“基准事实源”。当前模型事实源是 v1.1 加 v1.2—v1.9 的累计覆盖链；Phase 7 展示边界再叠加 v2.0；
- 不得根据摘要、旧状态表或旧交接说明，反向覆盖正式规格中的较新规则。

## 2. 每份文档负责什么

| 文档 | 当前职责 | 使用边界 |
|---|---|---|
| v1.1 Implementation Spec | 研究问题、系统边界、政策与场景、公式、时间线、实验矩阵、指标、日志 Schema、阶段 0—8 基础 DoD | 是基准规格，但冲突项必须应用 v1.2—v1.9 覆盖 |
| v1.2 Acceptance Spec | 整数初值与付款、Repair Aid 资格、ArriveID、损伤清单、正式压力场景、性能环境等已批准修正 | 只覆盖明确列出的 v1.1 条目 |
| v1.3 Acceptance Spec | 第一版 MVP 范围、四方法验证矩阵、数据契约以及 Phase 0—3 验收记录 | 研究范围继续有效；20 人 Persistent 上限等条目已被 v1.4 覆盖 |
| v1.4 Architecture Spec | 全员轻量 CoreState、状态与计算分离、唯一权威来源、统一动作和竞争、Phase 4 验收 | 是 v1.6 Current Proposed 的历史架构基础；全员动态状态和个人竞争已被 v1.7 明确覆盖 |
| v1.5 Unified Runtime Spec | 单一权威 Runtime、四方法共享语义、统一小时管线、Phase 5 入口与 DoD | 精确 Cohort 一致性要求已被 v1.6 覆盖 |
| v1.6 Phase 5.1 Spec | 受控 Cohort 近似、固定 Activation Trace、Simple 激活公平性、失败原子边界、运行模式和 Phase 6 前置架构债务 | 固定 Trace、Simple 公平性、动作定义、小时顺序和测量隔离继续有效；Proposed 的人口表示、个人候选/提交和旧日志语义已被 v1.7 覆盖 |
| v1.7 Cohort Batch Spec | Identity Registry、权威 Joint State、Action Flow、Batch Claim/Event、聚合 Ledger、Capsule、Dynamic Lift/Restrict、新 Digest/Schema 和 6G-B0—B5 | 是 6G-B 及最终 Proposed 的最高优先级规则；B1/B2 仍由 v1.6 运行，B3 才一次性切换 Macro 权威，B4 接入完整动态 LOD；各检查点必须标明 authority mode，不得长期双写或形成混合真相 |
| v1.8 Pre-Formal Hardening Spec | 正式资格与领域摘要分离、连续性大小误差、批量运行顺序/续跑、分项内存和 5 Seed 预检查 | H0—H5 的证据和实验工具规则继续有效；“不修改 v1.7 行为模型”的阶段边界已完成，后续住房定向修正以 v1.9 为准 |
| v1.9 Home Continuity Spec | 每栋房紧凑状态、批量维修对应具体 HomeID、Lift 优先读取房屋状态、住房承诺任务计时和 H6 验收 | 只修住房连续性，不改变 Cohort 规划、批量竞争、资源公式、Active=50 或其他方法；是 H6 的最高优先级规则 |
| v2.0 Visual Demo Spec | 正式/互动模式互斥、只读展示接口、真实 ResidentID 代理、空间布局、PCG/World Partition/HLOD、望远镜追踪、Dear ImGui、Phase 7-0—7F 验收 | 只是 Demo/Presentation 协议；模型仍为 v1.9，不能把空间、UI、Actor 或互动记录写回领域状态或正式实验 |
| Phase 7 Visual Demo Handoff v1.0 | Phase 7 开始前的 Git、源码、UE 内容、既有证据、风险和实施导航 | 是导航文档，不新增规则；2026-08-23 的确认项已转入 v2.0，冲突时以 v2.0 为准 |
| Phase 7-0 Checkpoint | Phase 7 分支、v2.0、索引、交接补充和作者/Codex 后续职责 | 是文档阶段验收记录；已于 2026-08-23 经作者确认并以 `a092164` 封板，不代表地图、PCG、UI 或 Actor 已实现 |
| Phase 7A Checkpoint | 无画面 Demo 模式、原子观察集合、一个追踪居民、只读值快照、命令记录/回放和正式数据隔离 | 是无地图连接层验收记录，不代表空间格子、20k 有画面性能、PCG、UI、代理或 Actor 已实现；必须经作者确认后才能进入 7B |
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
| Phase 6G-B5E Checkpoint | 20k 下四组交错顺序的 Proposed/Per-Agent 最终工程测速 | 判断 3 倍工程目标是否在顺序对调后稳定成立；不是论文正式统计，正式实验前加固完成并再次确认前不开放正式实验资格 |
| Phase 6H Plan / H0 Checkpoint | 把加固拆成 H0—H5，并在看结果前固定指标、5 Seed 和复核线 | 只冻结范围和验收方法，不代表 H1—H5 已实现 |
| Phase 6H-H1 Checkpoint | 领域摘要与正式资格分离、三项资格记录、新旧摘要映射和 Phase 6G 回归 | 证明管理标签不改变领域摘要；H2—H5 完成前模型正式资格仍关闭 |
| Phase 6H-H2 Checkpoint | 金钱、补助、木材和任务时间的误差大小，内部编号降为诊断，以及真实 200 人单 Seed 结果 | 证明新尺子可重建；任务计时已触发复核线，必须由 H5 多 Seed 分析，不能静默删除或直接调模型 |
| Phase 6H-H3 Checkpoint | 固定乱序、重复编号、运行清单、续跑、失败记录和无界面实验入口 | 证明批量运行可以重建并安全接续；只是 Development 工程验证，不等于正式 Shipping 实验 |
| Phase 6H-H4 Checkpoint | Identity、群体、Active、连续性记忆、批量请求/事件、账本等权威容器的分项内存和 50k/100k 趋势 | 证明已追踪权威内存的组成和增长方向；不包括整个 UE、输入输出副本或分配器开销，不能冒充绝对总内存 |
| Phase 6H-H5 Checkpoint | 5 Seed × 4 场景的 Oracle/Proposed 预实验、Routine 计时解释、住房可见误差和 41 项完整回归 | 工程工具与硬错误门通过；住房/首行动复核线需要作者决定，当前不能开放正式模型资格，也不能把 40 Runs 当正式统计 |
| Phase 6I-H6 Checkpoint | 每栋房紧凑状态、批量维修到具体房屋的对应、玩家靠近/离开时的住房连续性、住房承诺计时、5 Seed Pilot、20k 和 100k 复验 | H6-A—H6-F 已通过；作者已接受剩余维修排期差异并开放 v1.9 正式模型资格，40 Runs 仍不是正式统计 |

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
| 版本与 Digest | B1 Shadow 仍以 v1.6/Schema 1.1 为权威；当前 Proposed 使用 v1.9 和 `1.9-domain-v1` 领域摘要，不包含 Formal、日志 Schema、构建或机器信息；旧值与 v1.7 映射见 H1 Checkpoint | v1.7 §1、§11；v1.8 §4；v1.9 §7；H1 Checkpoint |
| 正式资格与领域摘要 | 正式资格是实验管理信息，不进入模拟领域摘要；正式 Run 必须同时满足模型已批准、本次明确请求、环境合格和硬错误为 0 | v1.8 §4 |
| 当前正式模型资格 | v1.9 已由项目作者批准；普通工程运行和既有 Pilot 不会因此自动成为正式数据 | v1.9 §11；Phase 6I-H6 Checkpoint §10 |
| 连续性误差 | 身份仍是零错误门；钱、补助、木材、房屋、任务和 FirstAction 同时报告不一致率与误差大小；EventID/ReservationID 只作内部编号诊断 | v1.8 §5 |
| 内存结论 | `memory_mb` 是整个进程；Proposed 另外报告各权威容器的已追踪字节，未测量前不得宣称某个容器是主要问题 | v1.8 §7 |
| 住房连续性 | Joint State 继续负责离屏社会总量和决策；另为每个 HomeID 保存一个紧凑房屋状态，维修中的房屋临时指向对应 Batch Event；Lift 必须先读这份状态 | v1.9 §3—§6 |
| 房屋变化成本 | 只允许在实际发生地震、维修开始或完成时按变化房屋数更新；不得每小时扫描全部 Identity，也不得为批量维修生成逐人事件或交易 | v1.9 §3、§5 |
| 住房承诺计时 | 原通用任务指标继续保留；RestoreHome 的进行中状态和剩余时间另行报告，不能让 Routine 周期差异掩盖维修承诺 | v1.9 §8 |
| 模型版本与 Demo 版本 | UI 必须把模型显示为 v1.9、领域摘要显示为 `1.9-domain-v1`；v2.0 只显示为 Demo 协议，不能冒充模型升级 | v2.0 §1—§2 |
| 正式/互动模式 | 正式模式只运行冻结固定轨迹；互动 Demo 只运行镜头/望远镜选择，两者初始化后互斥且不能运行中切换 | v2.0 §3 |
| UI/Actor 权限 | UI、Actor、动画、Blueprint 和 ImGui 只读取复制快照；暂停、倍率、选择和追踪只能作为有类型请求交给 Controller，不能直接写领域容器 | v2.0 §4、§11 |
| 可见居民 | Cohort 轻量代理必须绑定真实 ResidentID，但不承诺精确个人行为；完整 NPC Actor 必须绑定真实 Active ResidentID；无身份假人群禁止 | v2.0 §5—§6 |
| 展示空间与 PCG | 固定 Visual World Layout 是 NPC 空间目录与 PCG 的共同来源；NPC 不扫描 PCG Actor 判断房屋/道路；PCG 第一版编辑器生成并保存，World Partition/HLOD 负责加载和远景 | v2.0 §7—§8 |
| 互动 Demo 时间与规模 | 初始 1x=每真实秒 1 游戏小时且每帧最多一个 StepHour；2k 开发、20k 默认 Demo、100k 可选压力；Day 0 进入、Day 60 停止 | v2.0 §10 |

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

当前位于 `phase-7-visual-demo`，已提交 HEAD 为 `a092164`（Phase 7-0 文档封板）。该分支于 2026-08-23 从已封板的 H6-F 基准 `a9385b2` 创建。项目作者已确认 Phase 7-0 并授权进入 Phase 7A；Phase 7A 的无画面 Demo 连接层、专项检查和完整回归已经完成，当前改动尚未提交，等待作者检查点确认。尚未开始 Phase 7B、地图、PCG、Dear ImGui、代理或 NPC Actor。作者确认前不得提交 Phase 7A 或进入 7B；所有提交均未推送。

## 5. 后续修改规则

- 不回写历史版本来伪装规则从未变化；
- 新冲突必须先讨论，再用新的版本化覆盖文档记录；
- 新覆盖必须写明旧条目、替代规则、原因、影响和批准人；
- 阶段状态、Git 现场和测试结果写入检查点，不写成本索引中的永久模型规则；
- 按 v1.7 §14，所有新验收文档必须先用大白话解释，再补充必要的技术名称；不能假设项目作者预先懂术语，并且必须说明每项证据“能证明什么、不能证明什么”；
- 开始任何后续实现前，先检查本索引、当前 Git 状态、对应阶段的最高版本规格和检查点。
