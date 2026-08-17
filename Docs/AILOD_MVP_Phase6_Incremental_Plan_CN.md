# AILOD MVP Phase 6 增量实施计划与检查点

**计划版本：1.0**<br>
**日期：2026-08-17**<br>
**分支：`phase-6-backend-observer-logs`**<br>
**Phase 5.1 基线提交：`7ea750d6617f6346de37e19e2d20c9c76f5b682f`**<br>
**Step 0 计划提交：`a3a34969be04036fe919f9599ace609f0f508ceb`**<br>
**Phase 6A 封板提交：`71e3565`**<br>
**当前状态：Phase 6B 实现与自动验收通过；依项目作者本轮授权继续 6C，未推送。**

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
| Phase 6B | 建立最小 `ISimulationBackend` 边界 | 实现与自动验收通过，等待封板提交 |
| Phase 6C | 建立只读 Observer/Event Sink | 未开始 |
| Phase 6D | 输出 Run Manifest 与原始 CSV/JSONL | 未开始 |
| Phase 6E | 建立批量 Experiment Runner 与离线指标重建 | 未开始 |
| Phase 6F | 分离测量成本并完成 Phase 6 集成验收 | 未开始 |
| Phase 6G（可选） | 只在证据确认瓶颈后执行定向优化 | 默认不执行 |

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

## 10. Phase 6G：证据触发的可选优化

6G 默认不执行。只有 6F 的成本分解明确证明某一项是主要瓶颈，并由项目作者批准后，才建立独立检查点，例如：

- 用稳定账户句柄替代高频字符串账户查询；
- 用增量总量替代 Performance 模式的重复完整审计；
- 对不影响个人连续性的普通离屏记录做定向批处理。

任何优化都必须保持领域结果、日志 Schema、硬错误门和确定性不变。不得为了制造速度优势而削弱 Per-Agent，或根据正式实验结果反向调算法。

## 11. Step 0 确认门

Step 0 只完成计划拆分，没有修改 Phase 6 代码。项目作者已于 2026-08-17 确认本计划。

Step 0 与 6A 已分别完成。项目作者随后确认 6A，并于 2026-08-17 明确授权在新分支按 6B → 6C → 6D 顺序继续实施；每一步仍须独立执行检查点，但本轮不在步骤之间暂停等待。
