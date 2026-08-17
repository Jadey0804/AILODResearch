# AILOD MVP Phase 6 检查点

形成与最终复验日期：2026-08-17<br>
分支：`phase-6-performance-measurement`<br>
Phase 5.1 基线提交：`7ea750d6617f6346de37e19e2d20c9c76f5b682f`<br>
Phase 6A—6E 封板提交：`71e3565 / c0e84d2 / eb44bf3 / 93282ed / 6ee6873`<br>
当前状态：Phase 6A—6F 实现与自动验收通过；项目作者已确认；本检查点随 6F 封板提交，未推送

## 1. Phase 6 完成了什么

Phase 6 已把 Phase 5.1 的统一模拟核心变成可执行、可观察、可重放和可测量的实验基础设施：

1. 唯一生产模拟支持 `Initialize / StepHour / Finalize`，阻塞入口只是同一会话的兼容包装；
2. Oracle、Proposed、PerAgent、Simple 只替换最小 Backend 能力，不复制 Clock、Scenario、Action、Ledger、Scheduler、竞争和事务规则；
3. 只读 Observer/Event Sink 能记录 T+1 状态和已提交事实，不取得可写 Runtime 引用，也不改变确定性结果；
4. 单一 Experiment Runner 能展开 `Method × Scenario × Seed`，保存共享输入、独立 Run 目录和可重放 Manifest；
5. Accuracy/Validation 原始日志能离线重建轨迹误差、政策效应、行为 TVD、连续性与硬错误；
6. Performance Run 能生成隔离后的 `performance_1s.csv`，离线重建基础性能统计与 PerAgent Speedup 输入；
7. 生产、验证、审计、快照、观察、序列化和写盘成本已经分开，不再把检查工具的开销伪装成 Proposed 算法成本。

这说明“实验机器”已经搭好，不说明论文的三个假设已经成立。

## 2. 三种运行模式的冻结边界

| 模式 | 生产规划 | 逐成员近似复算 | 完整 Audit | Snapshot/研究日志 | 主要用途 |
|---|---|---|---|---|---|
| Validation | 正常执行 | 可开启；本检查点开启 | 初始 + 每小时 | 开启 | 开发、分歧与正确性检查 |
| Accuracy | 正常执行 | 默认关闭 | 初始 + 每小时 | 开启 | Phase 8 准确性实验 |
| Performance | 正常执行 | 禁止 | 初始 + Run 末 | 关闭完整 Observer/NPC 日志 | Phase 8 性能实验 |

固定 200 人 Proposed/StateImport 检查中，三种模式的确定性 Digest 都是 `D326B24A3D74128C955667DB42E8F1BADA9BC9CD`。完整 Audit 次数为 `1609 / 1609 / 2`；只有 Validation 产生额外验证规划与 `validation_cpu_ms`，Performance 的 Snapshot 和 Observer 成本为 0。

## 3. 成本字段怎样解释

- `production_cpu_ms`：一小时生产推进的总成本，排除 Validation、Audit、Snapshot 和 Observer；序列化与写盘发生在会话结束后，也不包含在内。
- `macro_cpu_ms`：Proposed 的 Cohort 离屏路径或 Simple 的 Aggregate 规划路径；它是 Production 的组成部分。
- `micro_cpu_ms`：Active Micro 的个人规划；Oracle/PerAgent 的全部 Individual 规划也归入此项。
- `transition_cpu_ms`：固定 Activation Trace 的 LOD 转换成本；它是 Production 的组成部分但单独列出。
- `initialize / finalize / audit / snapshot / observer / serialization / file_write`：各自独立记录，不进入 AI Production。
- `memory_mb`：采样时 UE 进程的 Used Physical Memory，不是仅由模拟数据分配的净内存。

当前 `*_cpu_ms` 由单线程同步区段的高精度 elapsed timer 计量，是本项目对 AI 区段 CPU 成本的工程代理，不是操作系统提供的进程 CPU counter；正式实验必须通过固定硬件/构建、随机运行顺序和重复 Runs 控制调度噪声。

`performance_1s.csv` 按约一真实秒汇总一桶；Run 结束允许一个不足一秒的末桶。离线 P95/P99 使用 nearest-rank。CPU、内存、真实时间和 Manifest 的测量摘要都不进入领域 Digest。

从 6F 起，Manifest 含真实计时，因此两个相同 Seed Run 的 Manifest 不应再要求逐字节一致。确定性承诺仍是：六份 Accuracy 领域文件、行顺序、领域 Digest 和 Manifest 重放后的领域结果一致。

## 4. 离线重建与失败边界

- Accuracy/Validation 目录必须含 Manifest、三份 CSV 和三份 JSONL；缺文件、缺 Oracle、缺 Method/None 时拒绝生成误导汇总。
- Performance 目录必须只依赖 Manifest 与 `performance_1s.csv`；可重建 SampleCount，AI/Macro/Micro/Transition 的 Mean/P95/P99/Max，内存 Mean/Peak，Active/Queue Max，以及相对同场景、同 Seed、同人口 PerAgent 的 Mean/P95 Speedup 输入。
- Accuracy 与 Performance Run 不允许混在同一个离线汇总根目录，因为二者的数据边界和配对规则不同。
- 删除任一种 `metrics_summary.csv` 后，仅使用对应原始文件即可重建出逐字节一致的汇总。
- Performance 缺少 `performance_1s.csv` 时会明确失败；Manifest 重放会重新生成同配置、同领域 Digest 和相应模式产物。

## 5. 6F 四档工程冒烟

固定 Seed `20260810`，StateImport，Development Editor + NullRHI；每档人口运行 Proposed、Simple、PerAgent 各一次。下表是链路检查时的单次累计成本，不是正式论文数据：

| 总人口 | 方法 | Performance 桶 | Production ms | Macro ms | Micro ms | Audit ms |
|---:|---|---:|---:|---:|---:|---:|
| 200 | Proposed | 1 | 100.678 | 27.782 | 0.014 | 0.328 |
| 200 | Simple | 1 | 38.160 | 0.145 | 0.420 | 0.005 |
| 200 | PerAgent | 1 | 93.591 | 0.000 | 20.654 | 0.301 |
| 2,000 | Proposed | 1 | 724.119 | 302.874 | 0.017 | 3.109 |
| 2,000 | Simple | 1 | 38.242 | 0.146 | 0.432 | 0.005 |
| 2,000 | PerAgent | 1 | 663.964 | 0.000 | 245.525 | 3.186 |
| 10,000 | Proposed | 7 | 6078.471 | 3691.665 | 0.045 | 16.593 |
| 10,000 | Simple | 1 | 38.468 | 0.155 | 0.434 | 0.006 |
| 10,000 | PerAgent | 6 | 5571.973 | 0.000 | 3310.498 | 17.327 |
| 20,000 | Proposed | 18 | 18096.168 | 12677.464 | 0.048 | 36.713 |
| 20,000 | Simple | 1 | 42.406 | 0.175 | 0.481 | 0.006 |
| 20,000 | PerAgent | 18 | 17792.181 | 0.000 | 12299.481 | 37.415 |

所有 12 个 Run 都精确完成 67 游戏日，只生成两份 Performance 产物，最终 Runtime 硬错误为 0；200 人 Proposed 的 Manifest 重放保持冻结 Digest。

## 6. 对工程结果的诚实解释

这次冒烟不能证明 Proposed 更快。相反，在这一次固定顺序工程运行中，Proposed 从 200 到 20k 都没有快于 PerAgent；20k Proposed 的 Macro 路径约占 Production 的 70%。这是一条应正视的架构证据：当前瓶颈更像是 Cohort 分组、全员扫描、候选构造或共同提交路径，而不是 Validation、完整 Audit、Snapshot 或日志污染。

但这仍不是正式失败结论，因为只有一个 Seed、一次重复、固定运行顺序、Development Editor + NullRHI，并且 200/2k 的 Run 不足一真实秒，P95 等于单个末桶。正式 H1 仍必须在冻结硬件/Shipping 或批准构建、随机运行顺序和 90 次性能矩阵上判断。

因此 6F 只得出两个结论：

1. 实验基础设施和成本隔离已经通过；
2. 可选 6G 已有候选证据，但只能在作者确认 6F 并批准具体瓶颈调查后执行，不能为了制造 Speedup 削弱 PerAgent、改变领域结果或更换指标。

## 7. 自动验收结果

- UE 5.4 Development Editor 编译成功。
- NullRHI 最终全套 `AILODResearch`：`25/25 Success`、`0 Failed`、自动化错误 `0`、退出码 `0`。
- Phase 0—4：`14/14 Success`；Phase 5：`4/4 Success`；Phase 6：`7/7 Success`。
- 新增模式边界测试与四档性能日志/汇总测试均已独立通过。
- 既有 Accuracy 汇总在加入 Performance 解析后继续通过，删除后仍可逐字节重建。
- 人口、木材、Coin、事件所有权、Reservation、重复事务、重复完成、TaskReset、到期未结事件和连续性硬错误在最终全套回归中均为 0。

## 8. 尚未完成

- 项目作者已于 2026-08-17 确认整个 Phase 6，并授权独立提交 6F 后在新分支执行只测量、不优化的 6G-A。
- Phase 6G 默认不执行；如批准，应先做只读 Profile/瓶颈归因，再决定最小优化点。
- Phase 7 的地图、Actor、UI、望远镜、可视化与演示用 King Policy 尚未开始。
- Phase 8 的 Pilot、480 次准确性正式 Runs、90 次性能正式 Runs、统计区间和论文结论均未开始。

本检查点随 6F 封板提交，未推送。后续 6G-A 必须使用独立分支和检查点；6G-B 仍需项目作者再次确认。
