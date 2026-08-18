# AILOD MVP Phase 6 增量实施计划与检查点

**计划版本：1.3**<br>
**日期：2026-08-17**<br>
**分支：`phase-6g-macro-profile`**<br>
**Phase 5.1 基线提交：`7ea750d6617f6346de37e19e2d20c9c76f5b682f`**<br>
**Step 0 计划提交：`a3a34969be04036fe919f9599ace609f0f508ceb`**<br>
**Phase 6A 封板提交：`71e3565`**<br>
**Phase 6B 封板提交：`c0e84d2`**<br>
**Phase 6C 封板提交：`eb44bf3`**<br>
**Phase 6D 封板提交：`93282ed`**<br>
**Phase 6E 封板提交：`6ee6873`**<br>
**Phase 6F 封板提交：`c629bb6`**<br>
**当前状态：Phase 6G-A 实现与自动验收通过，项目作者已于 2026-08-18 确认并授权本地封板提交；6G-B 正在进行规则审查、尚未开始实现；所有提交均未推送。**

本文件只把既有 Phase 6 规则拆成可独立验收的实施步骤，不新增研究模型、公式、参数、方法或日志字段。规则冲突仍按 `AILOD_MVP_Current_Rules_Index_CN.md` 指定的 v1.1 → v1.6 顺序处理。

## 1. Phase 6 最终要交付什么

Phase 6 只负责建立可信的实验基础设施：

1. 同一权威模拟可以初始化、逐小时推进和结束；
2. 四种方法只替换各自的 LOD Backend，不复制 Scenario、Action、Ledger、Scheduler 或时间管线；
3. Observer/Event Sink 只读观察，不改变模拟；
4. 单一生产入口能运行 `方法 × 场景 × Seed`；
5. 每个 Run 保存 Manifest 和冻结 Schema 的原始 CSV/JSONL；
6. 删除汇总文件后，能只用原始日志完整重建指标；
7. 算法、Audit、Snapshot、Observer 和文件写入成本分开测量。

Phase 6 不负责：

- Phase 7 的地图、UI、Actor、望远镜和动态 King 演示；
- Phase 8 的 Pilot 调参；
- 480 次准确性正式 Runs 或 90 次性能正式 Runs；
- 在没有成本证据前优化字符串账户查询、全人口扫描或事件存储；
- 改变已冻结的人口、政策、场景、公式、Seeds、Runs、Active Cap、方法边界和日志字段。

## 2. 逐步实施规则

每一步都使用以下流程：

1. 只实现当前一步，不预做下一步；
2. 执行 Development Editor 编译、全部既有自动测试和本步新增测试；
3. 更新本文件的状态与证据，明确完成内容、未完成内容和风险；
4. 停止实施，保持下一步为“未开始”，等待项目作者确认；
5. 作者确认后，将当前步作为独立本地提交，再开始下一步；
6. 未经明确要求不推送。

任何冻结 Digest 变化都必须逐项解释。任何硬错误、测试失败、不同入口结果不一致或观察器改变结果，都阻止进入下一步。

## 3. 总体检查点

| 检查点 | 目标 | 状态 |
|---|---|---|
| Step 0 | 冻结本增量计划、每步范围和确认流程 | 作者已确认 |
| Phase 6A | 建立 `Initialize / StepHour / Finalize` 生产会话 | 作者已确认，已封板 |
| Phase 6B | 建立最小 `ISimulationBackend` 边界 | 作者已确认，已封板 |
| Phase 6C | 建立只读 Observer/Event Sink | 作者已确认，已封板 |
| Phase 6D | 输出 Run Manifest 与原始 CSV/JSONL | 作者已确认，已封板 |
| Phase 6E | 建立批量 Experiment Runner 与离线指标重建 | 作者已确认，已封板 |
| Phase 6F | 分离测量成本并完成 Phase 6 集成验收 | 作者已确认，已封板 |
| Phase 6G-A | 只测量 Proposed Macro 子阶段并归因瓶颈 | 作者已确认，本次提交封板 |
| Phase 6G-B（可选） | 审查并处理 6G-A 证实的最大瓶颈 | 规则审查中，尚未开始实现 |

## 4. Phase 6A：可逐小时推进的生产会话

### 范围

- 在当前统一 Runtime 外建立一个最小会话门面；
- 对外提供 `Initialize / StepHour / Finalize`；
- 每次 `StepHour` 完整执行 v1.6 冻结的一小时时序；
- 现有阻塞式 `FUnifiedSimulationRunner::Run` 保留为兼容入口，但内部只能调用同一会话，不得保留第二套小时循环；
- 本步不建立 Backend 接口、不写文件、不计算新指标。

### 6A 检查点

- 阻塞入口与逐小时入口在 200 人、四方法、四场景、固定测试 Seed 下产生相同 Digest、事务数、事件数、最终状态和硬错误结果；
- 会话恰好推进 1608 个小时步，从 D-7 到 D60；D60 不产生新计划；
- `Audit` 与 `Snapshot` 仍观察同一 T+1 状态；
- Phase 0—5 的 18 项测试继续通过，并新增会话等价性测试；
- 编译与测试证据写入本文件后停止，等待作者确认 6A。

### 6A 实施与验收记录（2026-08-17）

- 新增公开的 `FUnifiedSimulationSession` 门面，提供 `Initialize / StepHour / Finalize`、完成状态、当前时间和完成小时数；内部 Runtime 继续保持私有。
- 原阻塞入口 `FUnifiedSimulationRunner::Run` 现在只负责创建并驱动同一个 Session；旧的 Runtime 67 日循环已移除，每小时权威逻辑只存在于 `FUnifiedRuntime::StepHour`。
- 会话状态阻止初始化前推进、重复初始化、D60 前 Finalize、D60 后继续推进和重复 Finalize；终点保护拒绝任何 `Clock >= D60` 的额外小时。
- 新增 `AILODResearch.Phase6.SessionLifecycleAndParity`，逐组验证 200 人下四方法 × 四场景。
- 16 组阻塞入口与逐小时入口均执行 1608 个小时步，停在 D60；最终两国 Snapshot 均标记为 T+1 的 D60。
- 16 组 Digest、Transaction、Event 和生产规划次数与 Phase 5.1 §5 冻结基线逐项一致；没有领域结果迁移。
- UE 5.4 Development Editor 编译成功。
- NullRHI 全套 `AILODResearch`：`19/19 Success`，`0 Failed`，自动化错误 `0`，最终退出码 `0`。
- 本步未建立 `ISimulationBackend`，未加入 Observer、文件日志、Experiment Runner、离线指标或性能优化；Phase 6B 仍为未开始。
- 项目作者已于 2026-08-17 确认 6A 检查点，并授权在新分支继续实施 6B、6C、6D；本步随封板提交保存，不推送。

## 5. Phase 6B：最小 Backend 边界

### 范围

- 引入最小 `ISimulationBackend`；
- Backend 只负责方法允许不同的部分：人口表示、离屏规划粒度、激活/降级桥接和方法专属结果读取；
- Runtime 继续唯一拥有 Clock、Scenario/Policy、Ledger、Scheduler、Event/Reservation、统一竞争、动作提交、Audit 和小时顺序；
- Oracle、Proposed、Per-Agent、Simple 不得复制共享领域规则；
- 本步不写正式日志，也不实现离线指标。

### 6B 检查点

- 顶层生产会话只选择一次 Backend；方法特有的表示、规划和桥接逻辑通过 Backend 进入；
- Scenario、Action、Ledger、Scheduler、竞争与提交仍只有一份实现；
- 6A 的 16 个方法/场景结果与 Phase 5.1 基线一致；如结构重构改变 Digest，必须证明领域结果逐项一致并解释唯一原因，否则失败；
- Oracle 仍拒绝 200 人以上，三个可部署方法继续通过 2k/10k/20k StateImport 冒烟；
- 全套既有测试和 Backend 边界新增测试通过后停止，等待作者确认 6B。

### 6B 实施与验收记录（2026-08-17）

- 新增私有最小 `ISimulationBackend`，只暴露方法身份、人口表示、离屏规划粒度、激活桥接和人口约束；Backend 不持有 Clock、Scenario、Ledger、Scheduler、Event、Reservation 或动作提交逻辑。
- 顶层 Runtime 构造时只调用一次 `CreateSimulationBackend`；四方法的原始枚举分支已从 Runtime 领域流程移除，方法映射只保留在 Backend 工厂。
- Oracle/Per-Agent 使用持久个人状态与 Individual 规划；Proposed 使用持久个人状态与 Cohort 离屏规划；Simple 使用王国聚合状态、Aggregate 规划和激活时重建 Micro 的桥接。
- 初始化、账本账户、资源读取、人口审计、地震、政策援助、激活/降级、规划入口与结果读取均通过 Backend 能力进入；共享小时顺序、政策、竞争、动作和事务实现未复制。
- 新增 `AILODResearch.Phase6.BackendBoundary`，验证四种 Backend 的表示/规划/桥接外部行为及 Oracle 200 人边界。
- 16 组 200 人方法 × 场景 Digest、事务、事件和生产规划次数与 Phase 5.1 冻结基线完全一致。
- Oracle 继续拒绝超过 200 人；既有 2k/10k/20k × Simple/Per-Agent/Proposed 的 StateImport 全时段冒烟继续通过。
- UE 5.4 Development Editor 编译成功；NullRHI 全套 `AILODResearch`：`20/20 Success`，`0 Failed`，自动化错误 `0`，最终退出码 `0`。
- 本步未加入 Observer、文件日志、Experiment Runner、离线指标或性能优化；依项目作者本轮授权，6B 独立封板后继续 6C。

## 6. Phase 6C：只读 Observer 与 Event Sink

### 范围

- 为会话增加只读观察出口；
- Observer 接收权威 T+1 小时状态，Event Sink 接收已经提交的事件、事务、LOD 转换和激活观察；
- 观察器只能收到不可修改的记录或副本，不能取得 Runtime、Ledger、CoreState 或 Backend 的可写引用；
- Observer 关闭时不得改变运行路径；
- 本步先保留内存记录，不写磁盘文件。

### 6C 检查点

- 同一配置分别使用空 Observer 和记录 Observer，Digest、事件顺序、事务、激活结果和硬错误完全一致；
- 回调时间单调，小时 Snapshot 与 v1.6 的 T+1 语义一致；
- 记录数量能与权威 Event Store、Ledger、转换和 Snapshot 对账；
- Observer 不能提交动作、推进时间或改变资源；
- 全套测试和无观察副作用测试通过后停止，等待作者确认 6C。

### 6C 实施与验收记录（2026-08-17）

- 新增公开的 `IUnifiedSimulationObserver` 与 `IUnifiedSimulationEventSink`；接口只接收 `const` 记录或当次值副本，不暴露 Runtime、Backend、Ledger、Event Store、Scheduler 或居民容器。
- Observer 每小时接收同一次推进完成后的 T+1 两国权威状态；每 6 小时附带稳定排序的非空 Cohort 观察，并在 FirstAction 确定后接收固定激活 NPC 快照。
- Event Sink 接收已经创建成功的事件、已经提交的账本事务、已经提交的 LOD 转换和 FirstAction 已确定的激活观察；记录顺序稳定。
- 统一生产 Runtime 现保存 120 次固定 Trace 转换。转换记录复用居民进行中动作已有的 `ArriveID`，不向 Scheduler 申请新号，因此表示切换不会扰动竞争顺序或冻结领域结果。
- 新增 `AILODResearch.Phase6.ReadOnlyObservation`：同一 Proposed/StateImport 配置分别关闭与开启 Observer/Event Sink，Digest、事件、事务、转换、激活、诊断与硬错误完全一致。
- 该测试对账 `40334` 个事件、`13109` 笔事务、`120` 次 LOD 转换、`60` 次激活和 `60` 个 NPC 快照；1608 个小时回调严格单调到 D60，Cohort 只在 6 小时边界出现。
- Observer 为构造只读视图发生的账户查询不进入生产 `LedgerQueryCount`；本步尚未写磁盘，观察成本的正式计时仍留给 6F。
- 16 组 Phase 5.1 冻结 Digest 继续完全一致；2k/10k/20k 三种可部署方法的 StateImport 全时段冒烟继续通过。
- UE 5.4 Development Editor 编译成功；NullRHI 全套 `AILODResearch`：`21/21 Success`，`0 Failed`，自动化错误 `0`，最终退出码 `0`。
- 本步未写 Manifest/CSV/JSONL，未实现 Experiment Runner、离线指标或性能优化；依项目作者本轮授权，6C 独立封板后继续 6D。

## 7. Phase 6D：Manifest 与原始日志

### 范围

- 使用 6C 的只读记录生成每 Run 独立输出目录；
- 实现 `run_manifest.json`；
- 实现 `kingdom_timeseries.csv`、`cohort_timeseries.csv`、`npc_snapshots.csv`；
- 实现 `simulation_events.jsonl`、`lod_transitions.jsonl`、`ledger_transactions.jsonl`；
- 所有文件保留 v1.1 冻结的公共标识字段和 Schema；
- `metrics_summary.csv` 留给 6E，`performance_1s.csv` 留给 6F。

### 6D 检查点

- 每个输出文件可由独立解析器读取，表头、类型、必填字段和记录粒度符合 v1.1 §31；
- Manifest 保存方法、场景、Seed、ConfigHash、Git commit、UE/构建/硬件和日志模式；
- 原始领域记录能与内存权威结果逐项对账；
- 相同 Seed 的确定性领域字段与行顺序一致；真实时间、CPU 和环境字段不得进入确定性 Digest；
- 开启或关闭文件写入不改变模拟结果；
- 全套测试和日志 Schema/回放测试通过后停止，等待作者确认 6D。

### 6D 实施与验收记录（2026-08-17）

- 新增 `FUnifiedRunLogWriter`，同时实现 6C 的只读 Observer/Event Sink；模拟期间只收集值副本，只有生产会话成功 Finalize 后才由调用方执行文件写入，文件系统不能取得 Runtime 引用。
- 每个 Run 只输出本步规定的七个文件：`run_manifest.json`、三个原始 CSV 和三个原始 JSONL；没有提前生成 `metrics_summary.csv` 或 `performance_1s.csv`。
- 三个 CSV 的表头直接由冻结 `AILODLogSchema` 生成；三个 JSONL 的每条记录都包含 `schema_version / experiment_id / run_id / method / scenario / seed / game_time` 及各自冻结字段。
- Manifest 保存 Spec/Schema、方法、场景、Seed、ConfigHash、三份输入 SHA-256、Git commit、UE 版本、构建类型、硬件、日志模式、开始/结束时间、有效性和可重建的运行参数；SHA-256 必须是 64 位十六进制。
- `RunManifestFields` 补入 v1.1 §31 文字已经明确要求、但原字段登记表遗漏的 `log_mode`；这只修正 Schema 登记缺口，不新增研究变量。
- 新增 `AILODResearch.Phase6.RawRunLogging`：独立解析 Manifest、每一行 CSV 和每一行 JSONL，并逐字段验证冻结表头、必填字段、公共身份、记录数量与顺序。
- Proposed/StateImport 工程 Run 对账结果：1608 个小时观察、3534 条 Cohort、60 个 NPC、40334 个事件、120 次 LOD 转换、13109 笔事务；全部与内存权威结果一致。
- 同一 Seed 的两个独立 Run 产生逐字节一致的六份原始领域文件；测试夹具固定环境元数据后 Manifest 也一致。真实开始/结束时间、硬件和后续 CPU 字段不进入领域 Digest。
- 关闭日志、Run A 开启日志、Run B 开启日志的确定性 Digest 均为 `D326B24A3D74128C955667DB42E8F1BADA9BC9CD`；开启文件写入不改变模拟结果。
- 16 组 Phase 5.1 冻结 Digest 继续完全一致；2k/10k/20k 三种可部署方法的 StateImport 全时段冒烟继续通过。
- UE 5.4 Development Editor 编译成功；最终 NullRHI 全套 `AILODResearch`：`22/22 Success`，`0 Failed`，自动化错误 `0`，最终退出码 `0`。
- 本次只运行工程检查点，没有运行 Pilot、480 次准确性正式 Runs 或 90 次性能正式 Runs；Experiment Runner/离线指标仍属于 6E，性能成本分离仍属于 6F。
- 项目作者已于 2026-08-17 确认 6D 检查点，并授权 6D 独立封板后在新分支开始 6E；本步仍不推送。

## 8. Phase 6E：Experiment Runner 与离线指标

### 范围

- 建立一个无 Actor 的生产入口，按配置运行 `方法 × 场景 × Seed`；
- Runner 只能创建并驱动 6A 的生产会话；旧 Phase 2/3 Runner 只保留历史回归标签，不能生成正式 Proposed 数据；
- 离线 Evaluator 只读取 6D 原始文件，不读取 Runtime 内存；
- 输出 `metrics_summary.csv`；
- 实现轨迹误差、政策效应误差、行为 TVD、连续性字段误差、硬错误和基础性能汇总。

### 6E 检查点

- 一个固定的小型工程矩阵可由单一入口自动完成；该矩阵不是 Pilot 或正式数据；
- 每个 Run 可由 Manifest 重新构造相同配置并重现确定性领域结果；
- 删除 `metrics_summary.csv` 后，仅使用原始日志即可重建出相同汇总；
- Oracle 配对、None 政策基线和缺失文件处理都有明确、可测试的有效性结果；
- 指标公式与 v1.1 §25—30 一致，不根据结果更换主要指标；
- 全套测试和离线重建测试通过后停止，等待作者确认 6E。

### 6E 实施与验收记录（2026-08-17，作者已确认）

- 新增无 Actor 的 `FExperimentRunner`：单一入口按配置展开 `Method × Scenario × Seed`，并且直接创建、逐小时驱动和 Finalize 6A 的 `FUnifiedSimulationSession`；没有调用旧 Phase 2/3 Runner。
- 每个 Seed 先在 `Inputs/Seed-*` 保存一套共享 Phase 0 输入，再在 `Runs/<Method>-<Scenario>-<Seed>` 生成各 Run 的七份 6D 原始文件；工程矩阵不会在单个 Run 目录混入跨 Run 指标或 6F 性能文件。
- Runner 生成真实 SHA-256；测试再用系统独立实现对三个输入文件复算，Population、Damage、Persistent Pool 的 64 位 Hash 均与 Manifest 完全一致。
- Manifest 增补 `deterministic_digest`、七项 Runtime 权威硬错误计数和完整运行参数；`IdentityMismatch` 不写死为 0，而由离线 Evaluator 将 NPC Snapshot 与同 Seed 的 Phase 0 初始人口身份逐项核验。`ReplayFromManifest` 会重新生成 Phase 0 输入并校验 ConfigHash/SHA-256，然后重跑生产会话并验证确定性 Digest。
- 新增只读 `FOfflineMetricsEvaluator`：只从每个 Run 的 Manifest、CSV 和 JSONL 建立索引，不取得 Runtime 或 `FUnifiedRunResult` 引用；输出实验根目录下唯一的 `metrics_summary.csv`。
- 轨迹误差和政策效应误差严格使用 v1.1 §25—26 的归一化 MAE；只统计 Warm-up 结束后的正式 60 日窗口。人数/房屋、Forest、Market、Price、Treasury 分别使用 `N`、`16N`、`2N`、`P0=1`、`5N`，其中 `N=PopulationPerKingdom`，与每条 Kingdom 轨迹的初始化尺度一致。
- 政策效应必须同时找到同 Seed 的 `Method/None`、`Oracle/Policy` 和 `Oracle/None`；不发明 v1.1 尚未给出数值的 Onset 阈值。本步生成已冻结的主要 `E_policy`，阈值型次要时间指标须在正式实验前另行预注册。
- 行为 TVD 使用固定八类 `Routine / Work / BuyWood / ChopWood / RepairStart / RepairComplete / Wait / AidReceived`，按事件 Participant 数计权；连续性输出 13 个冻结字段的 Oracle 配对不匹配率；八项硬错误直接报告计数和 `target=0`。
- 性能采样仍归 6F：6E 只写 `Performance.SampleCount=0` 和明确的 `performance_1s.csv deferred to Phase6F`，没有伪造 CPU、内存或 Speedup 数据。
- 固定工程矩阵为 `Oracle / Proposed × None / StateImport × Seed 20260810`，共 4 个 Run；它只是检查数据链路，不是 Pilot，更不是 480/90 次正式实验。
- Proposed/StateImport 从 Manifest 重放后的七份文件逐字节一致，确定性 Digest 均为 `D326B24A3D74128C955667DB42E8F1BADA9BC9CD`；删除 `metrics_summary.csv` 后仅凭原始文件重建，结果逐字节一致。
- 自动测试分别遮蔽一份必需原始文件、Oracle Policy Run 和 Method/None Run，Evaluator 均拒绝生成误导结果并返回明确错误；文件恢复后不影响工程矩阵。
- UE 5.4 Development Editor 编译成功；最终 NullRHI 全套 `AILODResearch` 为 `23/23 Success`、`0 Failed`、自动化错误 `0`、退出码 `0`。
- 项目作者已于 2026-08-17 确认 6E 检查点，并授权 6E 独立提交后在新分支完成 6F；6E 提交仍不推送。

## 9. Phase 6F：测量隔离与 Phase 6 总验收

### 范围

- 分开记录生产算法、Validation 复算、Audit、Snapshot、Observer/序列化、文件写入和 LOD Transition 成本；
- 生成 `performance_1s.csv`，CPU/真实时间字段不进入确定性 Digest；
- 固定 Validation、Accuracy、Performance 三种模式的实际工作边界；
- 使用一个预先固定的工程 Seed 做 200、2k、10k、20k 冒烟，只验证 Runner、日志和测量链路；
- 更新 Phase 6 总检查点，不运行正式 480/90 Runs。

### 6F 检查点

- Performance 模式不执行逐成员近似复算或完整 NPC 日志；Run 末仍执行完整硬错误审计；
- Accuracy 模式保留研究日志和正确性门，但不默认执行逐成员 GOAP 验证；
- Validation 模式的额外开销不会被计入 Proposed 生产算法成本；
- `run_manifest.json` 能复现任一工程 Run，删除汇总后仍能完全重建指标；
- 所有阶段测试通过，人口/资源/事件/事务/连续性硬错误均为 0；
- 只报告“实验基础设施已通过”和工程成本分解，不宣称 Proposed 已达到正式性能或准确性目标；
- 更新 Phase 6 检查点后停止，等待作者确认整个 Phase 6，再决定是否进入 Phase 7。

### 6F 实施与验收记录（2026-08-17，作者已确认）

- `StepHour` 与 `Finalize` 现在分别记录生产算法、Cohort/Aggregate Macro、Individual/Active Micro、LOD Transition、Validation 复算、完整 Audit、Snapshot、Observer、Initialize 和 Finalize 成本；序列化与第一次文件发布写入由日志器单列。`production_cpu_ms` 明确排除 Validation、Audit、Snapshot、Observer、序列化和文件写入。
- 初始化完整 Audit 与 Performance 结束完整 Audit 都计入 `audit_cpu_ms`，不再混入 Initialize/Finalize；Validation 的逐成员计划、分歧比较和诊断更新整段计入 `validation_cpu_ms`，不进入 Proposed 生产成本。
- Experiment Runner 新增显式运行模式。Validation 开启逐成员近似复算、逐小时 Audit 与 Snapshot；Accuracy 关闭逐成员复算但保留逐小时 Audit、研究日志与 Snapshot；Performance 关闭逐成员复算、Snapshot、完整 Observer/Event Sink 和已完成事件保留，只做初始与 Run 末完整 Audit。
- Performance Run 每个目录只写 `run_manifest.json` 与 `performance_1s.csv`。采样按约一真实秒汇总，Run 结束允许一个不足一秒的末桶；`memory_mb` 是采样边界的进程 Used Physical Memory。CPU、内存和真实时间字段不进入确定性 Digest。
- Manifest 新增 `measurement_summary`，记录 Initialize/Production/Macro/Micro/Transition/Validation/Audit/Snapshot/Observer/Finalize/Serialization/File Write；因此从 6F 起 Manifest 含真实计时，本身不再要求逐字节确定。六份准确性领域原始文件、行顺序与确定性 Digest 仍可逐字节重放。
- 离线 Evaluator 现在能区分 Accuracy/Validation 与 Performance 目录。Accuracy 汇总不伪造性能样本；Performance 只读取 Manifest 与 `performance_1s.csv`，重建 SampleCount、AI/Macro/Micro/Transition 的 Mean/P95/P99/Max、进程内存、Active/Queue 以及相对 Per-Agent 的 Mean/P95 Speedup 输入。P95/P99 使用 nearest-rank；删除汇总后重建结果逐字节一致。
- 新增 `AILODResearch.Phase6.MeasurementModeBoundaries`：同一 Proposed/StateImport 的 Validation、Accuracy、Performance Digest 均为 `D326B24A3D74128C955667DB42E8F1BADA9BC9CD`；Audit 次数分别为 `1609 / 1609 / 2`，Validation 额外复算非零，Accuracy/Performance 为 0，Performance 的 Snapshot/Observer 成本为 0。
- 新增 `AILODResearch.Phase6.PerformanceLoggingScaleSmoke`：固定工程 Seed `20260810`，Proposed/Simple/PerAgent 在总人口 `200 / 2k / 10k / 20k` 共 12 个 67 游戏日 Performance Run 全部完成；每 Run 均产生至少一个合法性能桶、只含两份性能产物、Manifest 可重放、汇总可重建，最终硬错误为 0。
- 单次 Development Editor + NullRHI 工程冒烟显示：20k Proposed 的生产成本约 `18096 ms`，其中 Macro 约 `12677 ms`；同轮 PerAgent 约 `17792 ms`。这只能说明当前瓶颈主要位于 Proposed Macro 路径，并触发 6G 的候选证据；固定顺序、单 Seed、非目标 Shipping 环境不能用于声称正式 Speedup、P95 或显著性，也不能据此修改研究指标。
- UE 5.4 Development Editor 编译成功；最终 NullRHI 全套 `AILODResearch` 为 `25/25 Success`、`0 Failed`、自动化错误 `0`、退出码 `0`。其中 Phase 0—4 为 14 项、Phase 5 为 4 项、Phase 6 为 7 项。
- 本步没有运行 Pilot、480 次准确性正式 Runs 或 90 次性能正式 Runs，没有执行可选 6G 优化，也没有进入 Phase 7。项目作者已于 2026-08-17 确认 6F，并授权 6F 独立提交后在新分支把 6G 拆为“只测量的 6G-A”和“需再次确认的 6G-B”；本检查点随 6F 封板提交，不推送。

## 10. Phase 6G-A：Proposed Macro 定向 Profile

### 范围

- 只为 Proposed 的 Macro 路径增加可关闭的诊断计时和计数，不改变 Cohort Key、分组、代表、候选、竞争、提交、事件或账本语义；
- 把现有 Macro 总成本拆为居民扫描与分组、Cohort 代表合成/规划、成员分配与候选构造、候选排序、竞争准备、竞争检查、动作提交；
- Profile 字段只写入 Manifest 的诊断子对象和自动化日志，不改变冻结的 `performance_1s.csv` 字段；
- 使用固定工程 Seed `20260810`，在总人口 2k、10k、20k 的 Proposed/StateImport Performance Run 上执行；
- 对照 6F 冻结 Digest、硬错误和模式边界，确认开启 Profile 只增加观察成本，不改变领域结果；
- 只报告子阶段占比与候选瓶颈，不修改实现，不宣称正式性能或显著性。

### 6G-A 检查点

- Profile 默认关闭，只有显式工程请求才开启；
- 2k/10k/20k 的领域 Digest 与 6F 完全一致，所有硬错误为 0；
- Macro 子阶段时间非负、可解释，子项总和不超过带 Profile 的 Macro 总时间；
- Manifest 能保留 Profile 开关、时间和计数，重放保持相同领域 Digest；
- Development Editor 编译和全套自动测试通过；
- 更新 6G-A 检查点后停止，等待项目作者确认是否允许 6G-B。

### 6G-A 实施与验收记录（2026-08-17，作者于 2026-08-18 确认）

- 新增默认关闭的 `bEnableMacroProfiling` 工程开关；不开启时不记录子阶段计时，也不在 Manifest 写入 `macro_profile`。
- Profile 只在 Proposed 的 Cohort 粒度路径开启，按现有 Macro 计费边界记录居民扫描/分组、代表合成/规划、成员分配/候选构造、候选排序、竞争准备、竞争检查和动作提交七段累计时间，以及小时、居民访问、Cohort 和候选计数。
- Profile 结果只嵌入既有 `run_manifest.json` 的 `measurement_summary.macro_profile`；`performance_1s.csv` 的冻结表头和产物集合均未改变，Replay 能恢复 Profile 开关。
- 固定 Seed `20260810` 的 Proposed/StateImport Performance 定向测试在总人口 2k、10k、20k 全部完成，冻结 Digest 分别保持 `8DC871F8DE2969291D42C8CC49CB1F7E4433698E`、`9F0DD3AC2AF2B8E3523DC30F3F2516F751BD5137`、`9AB01FA7115EF32D31443F8831004CE55DE22D0E`，最终硬错误均为 0；2k Manifest 重放保持相同 Digest。
- 单独定向运行中，七个子阶段解释了 Macro 总时间的 `93.0% / 97.6% / 98.6%`。动作提交占已归因时间的 `66.1% / 85.2% / 90.8%`，是三档共同的第一瓶颈；居民扫描与分组占 `24.3% / 10.2% / 6.3%`，不是大规模下的第一瓶颈。
- 2k、10k、20k 分别访问居民 `3,216,000 / 16,080,000 / 32,160,000` 次，生成候选 `403,071 / 2,014,934 / 4,030,235` 个。每小时全员扫描和逐候选个人提交仍随人口增长；Cohort 总组数只有 `2,807 / 2,848 / 2,848`，说明“群体决策次数少”没有自动消除“个人承诺写入成本”。
- `ActionCommit` 当前仍是宽边界，包含动作复核、事件创建、Scheduler、Reservation/Ledger（按动作需要）、居民 CoreState 写回和失败后的 Wait 提交；6G-A 能定位到该边界，但不能只凭本次结果断言其中哪一种容器或调用是根因。
- Development Editor 编译成功；NullRHI 全套 `AILODResearch` 为 `26/26 Success`、`0 Failed`、自动化错误 `0`、退出码 `0`。全套回归中的第二次 Profile 仍得到相同 Digest、计数和相同第一瓶颈，绝对时间因运行环境而变化。
- 这些数据仍是单 Seed、单次、Development Editor + NullRHI 的工程定位数据，计时开关本身也有开销；它们不能作为正式 Speedup、P95、显著性或论文假设结论。
- 本步没有修改 Cohort Key、决策、候选、竞争、事件、资源或个人状态规则，没有优化实现，也没有运行 Pilot 或正式实验。独立 6G-A 检查点见 `AILOD_MVP_Phase6G_A_Checkpoint_CN.md`。项目作者已于 2026-08-18 确认本检查点并授权本地封板提交；6G-B 只进入规则审查，尚未授权实现。

## 11. Phase 6G-B：经确认后的最小定向优化

6G-B 默认不执行。只有 6G-A 明确证明某个子阶段是主要瓶颈，并由项目作者再次批准后，才建立独立分支和检查点。候选方向可以包括整数 Cohort Key、稳定分组缓存、稳定账户句柄或不破坏个人连续性的定向批处理，但不能提前选定。

任何 6G-B 优化都必须保持领域结果、冻结日志 Schema、硬错误门和确定性不变。不得为了制造速度优势削弱 Per-Agent、删除个人 CoreState/独立承诺，或根据正式实验结果反向更换指标。

## 12. Step 0 确认门

Step 0 只完成计划拆分，没有修改 Phase 6 代码。项目作者已于 2026-08-17 确认本计划。

Step 0 与 6A 已分别完成。项目作者随后确认 6A，并于 2026-08-17 明确授权在新分支按 6B → 6C → 6D 顺序继续实施；每一步仍须独立执行检查点，但本轮不在步骤之间暂停等待。
