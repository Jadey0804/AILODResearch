# AILOD MVP Phase 6G-B0 检查点

形成日期：2026-08-18<br>
分支：`phase-6g-b-cohort-batch`<br>
基线提交：`41d3bae`（Phase 6G-A 封板）<br>
当前状态：6G-B0 已由项目作者于 2026-08-18 确认；本检查点随 B0 独立本地提交封板，未推送；6G-B1 已获授权但尚未开始

## 1. 本步目标和边界

6G-A 已证明当前 Proposed 的群体代表决策很少，但 20k 仍生成约 403 万个个人候选；`ActionCommit` 占已归因 Macro 时间约 91%。项目作者批准不再把 6G-B 限制为保持旧 Digest 的函数级优化，而是先冻结 v1.7 的 Cohort 批量提交和动态解聚规则。

B0 只完成规则设计：

- 新增 v1.7 版本化覆盖规格；
- 更新当前有效规则索引；
- 把 6G-B 拆为 B0、B1、B2A、B2B、B3、B4、B5；
- 明确每步确认门和最终工程验收；
- 不修改 C++、测试、配置、当前领域 Digest 或日志实现。

## 2. 已冻结的核心决定

1. 全部居民永久保留静态 Identity，但未激活居民不再持有每小时更新的完整动态 CoreState；
2. Proposed 未激活人口由 `Kingdom × Profession × IncomeBand` 外层 Cohort 和稀疏动态 Joint Cell 权威表示；
3. Joint Cell 保存 HomeState、Commitment、PurchasingPowerBand、WoodBand、AidEligibility 的联合分布，不用独立边际相乘；
4. Cohort 每非空格子产生整数 Action Flow，不生成 ParticipantCount 个个人候选；
5. Macro Batch Claim 与 Active `Count=1` Claim 在同一资源 Scope 按确定性整数配额竞争；
6. 匿名离屏事件和事务按 Batch 保存，只为 Lift/Capsule 的稀疏特殊成员保存 ParticipantRef/Child Event；
7. Identity Registry、Cohort Joint State、Ledger、Batch Event、Active State、Capsule 和表现层各有唯一权威边界；
8. Capsule 保存上次观察和谱系条件，不是当前个人资源的第二真相，也不是第三套 AI；
9. Lift/Restrict 必须原子迁移人口、Ledger、Event/Reservation 和 Capsule；零时间往返总量与谱系不变；
10. v1.7 权威路径使用 `SpecVersion=1.7`、`SchemaVersion=1.2` 和新 Digest；旧 v1.6 Proposed 仅作为工程消融对照；
11. 一次性 Identity 初始化和静态内存允许 `O(N)`，但每小时后台不得扫描 Identity 或创建 `O(N)` 候选/事件/事务；
12. Simple、Per-Agent、Oracle、固定政策、共享 Domain、正式 Trace、Active Cap=50 和既有主要研究指标不变。

## 3. 与旧规则的冲突处理

| 旧规则 | B0 处理 |
|---|---|
| v1.4 全员准确动态 CoreState | 由 v1.7 Identity + Joint State + Active/Capsule 覆盖 |
| v1.4 Cohort 只读缓存 | 未激活 Cohort Joint State 改为权威动态表示 |
| v1.6 七维按居民分组 | 外层三维稳定 Key + 内层稀疏联合格子 |
| v1.6 每居民真实复核和独立事件 | Action Flow、Batch Claim/Event 与聚合 Ledger 提交 |
| v1.4/v1.6 逐 ResidentID 竞争 | Claim 级确定性整数配额，Active 使用 Count=1 |
| v1.6 Schema 1.1 和旧 Digest | v1.7 权威接管时升为 Schema 1.2 并冻结新 Digest |

没有回写或删除 v1.4/v1.6；权威索引明确 v1.7 只覆盖上述冲突，历史验收仍作为旧实现证据。

## 4. 逐步实施门

| 步骤 | 只做什么 | 下一步前必须证明 |
|---|---|---|
| B0 | 冻结规则和索引 | 文档无冲突，作者确认 |
| B1 | Shadow Cohort | 不修改权威结果；人口、资源、联合状态、行动人数可对账 |
| B2A | Wait/Routine Batch 切片 | 隔离夹具中不再逐人创建对象；不形成混合权威 |
| B2B | Work/Ledger Batch 切片 | 隔离夹具中工资/国库聚合事务守恒且幂等 |
| B3 | 全动作 Batch 与 Macro 权威切换 | Buy/Chop/Repair/Reservation、统一 Claim、原子失败和谱系正确；全部动作齐备后一次性切换 Joint State 权威 |
| B4 | Lift/Restrict | Identity/Capsule、资源提取写回、Batch Split/Merge 和 Active≤50 |
| B5 | 总验收 | 200 准确性；2k—20k 回归/性能；50k/100k 压力；硬错误 0 |

B0 获确认和独立提交前，不得实现 B1；后续任一步也不得提前实现下一步。

## 5. B0 检查结果

- 新增 `AILOD_MVP_Prototype_Phase_Acceptance_Spec_CN_v1.7.md`；
- 当前规则读取顺序已延伸为 v1.1 → v1.7；
- Phase 6 增量计划已改为 v1.7 驱动的 B0—B5；
- B2A/B2B 已明确为隔离 Batch 切片；B3 全动作齐备前不得让 Joint State 与 v1.6 个人状态形成混合权威；
- 分支从 6G-A 封板提交 `41d3bae` 创建，未混入其他代码；
- Source、Config、Tests 和现有运行产物均未修改；
- `git diff --check` 已通过；换行输出仍只是仓库既有 LF/CRLF 提示，没有 whitespace error；
- 四份 B0 文档引用到的 10 个规格/索引/检查点文件均存在；v1.7 最高优先级、B1 只读 Shadow、B2 隔离切片、B3 单次权威切换、B4 Dynamic LOD、Active Cap 和 Schema 版本的交叉断言均通过；
- Git 范围仅包含本步两份新增 Docs 和两份更新 Docs，没有 Source、Config、Tests 或其他文件；
- B0 是纯文档步骤，不重复运行未受影响的 UE 编译与 26 项二进制测试；最近一次可执行基线仍是 6G-A 的 Development Editor 成功与 `26/26 Success`。

## 6. 尚未完成

- 没有建立 Identity Registry、Joint State、Batch Claim/Event、Capsule 或 Lift/Restrict 代码；
- 没有改变当前 v1.6 Proposed 的权威运行结果；
- 没有生成 v1.7 ConfigHash、Schema 1.2 文件或新 Digest；
- 没有获得 20k Speedup、50k/100k 可运行性或正式准确性结论；
- 没有开始 Pilot、正式 480/90 Runs、Phase 7 演示或推送远端。

项目作者已于 2026-08-18 确认本检查点，并授权把 B0 独立本地提交后开始 B1 Shadow Cohort；B1 仍不得接管权威 Ledger/Event/Scheduler。
