# AILOD MVP Phase 8 正式实验最终检查点

日期：2026-09-01  
分支：`phase-8-formal-experiments`  
冻结正式实验提交：`7699bbffdbcca536758f243f84a73b466230d65d`  
状态：480 次正式准确性运行和 90 次正式性能运行均已完成并通过资格审计；统计与归档材料已生成。论文填数和答辩视频按作者要求暂缓。

## 1. 大白话结论

本阶段回答两个问题：大量 NPC 能否算得更省，以及省算力后结果还像不像逐人模拟。

- 2k 人时，Proposed 的固定管理成本较高，速度慢于 Per-Agent。
- 10k 人时，Proposed 开始快于 Per-Agent。
- 20k 人时，Proposed 相对 Per-Agent 的配对中位加速为 `3.943749` 倍。
- Proposed 的宏观轨迹、政策效应和行为分布明显比 Simple 接近 Oracle。
- ResidentID、HomeID、住房状态、当前目标、第一步行为和住房承诺事件总体保持连续。
- 普通 Routine 的 `TaskActive` 时间相位与 Oracle 大量错位。这是正式结果中的主要负面发现，必须在论文中正面报告。
- Proposed 的进程峰值内存高于 Per-Agent；现有证据不支持“Proposed 节省总内存”。

## 2. 正式数据资格

| 数据组 | 覆盖 | 构建 | 提交 | 资格结果 |
|---|---:|---|---|---|
| Accuracy | 480/480 Runs | Shipping | `7699bbf` | PASS |
| Performance 2k | 30/30 Runs | Shipping | `7699bbf` | PASS |
| Performance 10k | 30/30 Runs | Shipping | `7699bbf` | PASS |
| Performance 20k | 30/30 Runs | Shipping | `7699bbf` | PASS |

四组数据的 hard-error 总数均为 0。Pilot、Preflight、Development 和 Phase 7F 有画面工程数据没有混入 Phase 8 正式统计。

源 `metrics_summary.csv` 的 SHA-256：

| 数据组 | SHA-256 |
|---|---|
| Accuracy | `AD8C5F5B8BD84009FAB6AA230E0BB1467343EB18674C88F74333716F61AF4F5D` |
| Performance 2k | `CDB1F9C925ADB164651DFF1CF5D886F3C419D8D8C40C23CE700C0A5F19547D47` |
| Performance 10k | `6981FE66717AFD7AC0301E4C03AEA63BC7CFEF1A13599B12EED4552490E0E87C` |
| Performance 20k | `13AF33AD77745ACB8FEDA88A89BA2F143244826A53F428B027E7C531844654FE` |

## 3. 统计方法

准确性按相同 `Scenario + Metric + Seed` 配对。报告中位数、IQR 和均值；Proposed-Simple 与 Proposed-PerAgent 的均值差使用 `100000` 次确定性 paired percentile bootstrap，统计 Seed 为 `20260831`，报告 95% CI。

性能使用完整 67 天运行的 `Performance.AICpuMs.Total`。每个人口规模、每种方法有 10 次重复；报告 P50、P95、IQR、均值 95% t 区间和按 Repeat 配对的速度比。正式运行采用确定性全局乱序，另报告执行位置与总耗时的 Spearman 相关，避免把热状态和时间漂移藏起来。

这套方法遵守预冻结规则：没有事后构造综合准确性分数，也没有删除不利指标。

## 4. 主要正式结果

### 4.1 性能

| 人口 | Proposed P50 ms | Per-Agent P50 ms | Simple P50 ms | Proposed/Per-Agent 配对速度比 P50 |
|---:|---:|---:|---:|---:|
| 2k | 4528.2508 | 638.9519 | 35.8826 | 0.140255 |
| 10k | 4826.2935 | 6252.6388 | 35.9906 | 1.295480 |
| 20k | 4813.5017 | 19008.5822 | 35.7164 | 3.943749 |

Proposed 的成本在 2k—20k 之间接近固定；Per-Agent 随人口增大明显增长。Simple 很快，其准确性代价必须同时报告。

### 4.2 Behavior TVD

| 场景 | Proposed 中位数 | Simple 中位数 | Proposed-Simple 均值差 95% paired bootstrap CI |
|---|---:|---:|---:|
| None | 0.000322 | 0.781507 | [-0.780897, -0.778559] |
| HarvestCap | 0.000322 | 0.775135 | [-0.774522, -0.772588] |
| StateImport | 0.000322 | 0.785365 | [-0.784709, -0.782364] |
| RepairAid | 0.000432 | 0.779072 | [-0.778782, -0.778662] |

差值小于 0 表示 Proposed 误差更低。四个区间均远离 0。

### 4.3 StateImport 连续性

- `PersistentIDMismatchRate`：Proposed 中位数 `0`。
- `HomeIDMismatchRate`：Proposed 中位数 `0`。
- `HomeStateMismatchRate`：Proposed `0.033333`，Simple `0.166667`。
- `MoneyMAE`：Proposed `1.141667`，Simple `2.616667`。
- `InventoryWoodMAE`：Proposed `0.066667`，Simple `0.566667`。
- `CurrentGoalMismatchRate`：Proposed `0.016667`。
- `FirstActionMismatchRate`：Proposed `0.033333`。
- `TaskActiveStatusMismatchRate`：Proposed `0.908333`，Simple `0.091667`。

## 5. 预冻结阈值审计

四个场景的结论相同：

- Money、RepairCredit、InventoryWood、HomeState 和 FirstAction：`0/30` Seed 越过审查线。
- TaskActive：`30/30` Seed 越过 `0.10` 审查线。
- TaskRemainingHours：每场景 `1/30` Seed 越过 `4` 小时审查线。

`REVIEW` 的含义是必须检查并解释原始明细。它不会自动把整项研究判为失败。

## 6. TaskActive 分解

每个场景共有 `1800` 对正式 NPC 快照，其中 `1603` 对 TaskActive 不一致；池化不一致率 `0.890556`，逐 Run 中位率 `0.908333`。

四个场景中的方向和来源完全一致：

- `1603/1603` 都是 Proposed=Active、Oracle=Inactive；
- CurrentGoal 相同；
- FirstAction 相同；
- HomeState 相同；
- 双方都是 `RoutineLife / Routine`；
- 与 RestoreHome 等承诺事件相关的不一致为 `0`。

因此，正式结论是：Proposed 保住了身份、目标、第一步行为、住房状态和住房承诺；普通 Routine 的持续时间或观察相位没有对齐 Oracle。承诺任务指标是辅助诊断，不能替代原始通用 TaskActive 指标。

## 7. 可信度和限制

1. 性能正式运行采用 Shipping、NullRHI 和当前单机环境；结果不能直接外推到所有机器和所有地图。
2. 10 次性能重复中，部分方法的执行位置与总耗时存在正相关。全局乱序减轻了固定顺序偏差，仍不能完全消除长批次中的热状态和时间漂移。10k、20k 的方向与独立中位数比一致，20k 余量最大。
3. `memory_mb` 是整个进程 UsedPhysical。它包含 UE、输入输出、缓存和分配器成本，不能当成某个 Proposed 容器的独立占用。
4. Oracle 只用于 200 人准确性参照。大规模性能比较没有把不可扩展的 Oracle 当作主基线。
5. Phase 7F 有画面数据回答演示运行问题；Phase 8 NullRHI 数据回答模拟算法成本问题。两类证据不能互换。

## 8. 可复现材料

项目工具：

- `Tools/Phase8/Test-Phase8Output.ps1`：重新审计四组正式输出；
- `Tools/Phase8/Analyze-Phase8FormalResults.py`：重建配对统计、阈值表和 TaskActive 分解；
- `Tools/Phase8/Build-Phase8FormalWorkbook.mjs`：生成可阅读工作簿和逐 Seed 图；
- `Tools/Phase8/New-Phase8ArchiveManifest.ps1`：生成逐文件 SHA-256 归档清单。

派生材料：

- `outputs/01a05060-81af-7bc2-8300-7e4999a3edc9/AILOD_Phase8_Formal_Results_Analysis_CN_v1.1.md`
- `outputs/01a05060-81af-7bc2-8300-7e4999a3edc9/AILOD_Phase8_Formal_Analysis_Data_v1.1.json`
- `outputs/01a05060-81af-7bc2-8300-7e4999a3edc9/AILOD_Phase8_Formal_Results_Analysis_CN_v1.1.xlsx`
- `outputs/01a05060-81af-7bc2-8300-7e4999a3edc9/AILOD_Phase8_Formal_Archive_SHA256_v1.0.csv`

其中 CSV、JSON 和 Markdown 精简证据随项目归档；XLSX 是可由上述工具重新生成的本地阅读副本，按仓库规则忽略，不作为唯一事实源。

最终资格复核完成后重新生成归档清单：`3731` 个文件，共 `7746254463` 字节。归档树摘要为 `9B3B5E22F912900DA6B9DE8103F8BF164622BE28BD7F09C70E5378D3C33BBDD4`；清单文件 SHA-256 为 `BFE19870978B0D804442C399CE9FB1C2DFB4B38D9A83C516E8BF2E74AAD1B83E`。

正式 Shipping 可执行文件：

- 路径：`Binaries/Win64/AILODResearch-Win64-Shipping.exe`
- SHA-256：`D660ABB5E48B9CBF0B5189049CD0069FDE91178CA6CD05EBC83736F2F99782FE`
- 最后写入 UTC：`2026-08-31T06:27:57.6565448Z`

该可执行文件早于正式运行，且正式 manifest 均记录同一冻结提交与 Shipping 构建。

## 9. 论文可以写和不能写的内容

可以写：

- 20k 下 Proposed 相对 Per-Agent 的配对中位加速约 3.94 倍；
- 10k 开始出现性能优势，2k 存在固定成本劣势；
- Proposed 在宏观轨迹、政策效应和 Behavior TVD 上显著优于 Simple；
- 身份、住房和承诺事件连续性较好；
- 普通 Routine TaskActive 时间语义明显偏离 Oracle；
- 当前正式结果没有显示 Proposed 的进程峰值内存优势。

不能写：

- Proposed 在所有规模上都更快；
- Proposed 与 Oracle 完全一致；
- TaskActive 误差很小；
- Proposed 节省总内存；
- NullRHI 正式性能等于有画面 FPS；
- 一台机器的结果能代表所有硬件和地图。

## 10. 最终回归

- 命令范围：`Automation RunTests AILODResearch`
- 环境：UE 5.4 `UnrealEditor-Cmd`、`-NullRHI`、当前 HEAD `7699bbf`
- 结果：`73/73 Success`，`0` Failure，`0` Automation Error，进程退出码 `0`
- 日志：`Saved/Logs/AILODResearch.log`
- 日志 SHA-256：`60D2941BFE228389B5C2A59A012DD8E688C561634434DC85FC292D272AF4BFB6`

本次回归只验证当前代码和测试仍处于可交付状态；没有重跑 570 次正式实验。日志中的 Epic 遥测联网警告来自 `datarouter.ol.epicgames.com` 不可达，没有造成自动测试失败。

## 11. 当前关闭状态

Phase 8 正式数据采集、资格审计、统计分析、逐 Seed 图、阈值审计、TaskActive 分解、归档哈希和最终 73/73 自动回归已经完成。论文数据回填、论文审核、最终 PDF 和 2—4 分钟答辩视频按作者安排暂缓。本检查点、工具和精简派生证据与本次项目收口提交一并归档；没有 push。
