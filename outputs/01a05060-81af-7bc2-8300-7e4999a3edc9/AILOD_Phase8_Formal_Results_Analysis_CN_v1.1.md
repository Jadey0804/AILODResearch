# AILOD Phase 8 正式结果分析 v1.1

生成时间：2026-08-31T19:24:30.356812+00:00  
冻结代码：`7699bbffdbcca536758f243f84a73b466230d65d`  
统计随机种子：`20260831`  
配对 Bootstrap：`100000` 次，percentile 95% CI。

## 1. 数据资格

- Accuracy：480/480 Runs，通过 Shipping、Git、输入 Hash、硬错误和排除清单审计。
- Performance：2k/10k/20k 各 30/30 Runs，共 90/90。
- Accuracy hard-error 总数：0。
- Performance hard-error 总数：0。
- Pilot、Preflight、Development 和 Phase 7F 数据没有进入本报告。

## 2. 统计方法

- Accuracy 按同一 Scenario、Metric、Seed 配对。
- 报告中位数、IQR、均值，并对 Proposed-Simple、Proposed-PerAgent 的配对均值差做确定性 paired bootstrap。
- 差值小于 0 表示 Proposed 的误差更低。
- Performance 使用整场 `Performance.AICpuMs.Total`；10 次重复报告 P50、P95、IQR 和均值 95% t 区间。
- 没有构造综合准确性分数，也没有根据正式结果删除不利指标。

## 3. 性能结果

| Population | Proposed P50 ms | PerAgent P50 ms | Simple P50 ms | Proposed speedup P50 | Speedup Q1 | Speedup Q3 |
|---|---|---|---|---|---|---|
| 2000 | 4528.250800 | 638.951850 | 35.882550 | 0.140255 | 0.135124 | 0.142411 |
| 10000 | 4826.293500 | 6252.638800 | 35.990550 | 1.295480 | 1.253063 | 1.363022 |
| 20000 | 4813.501700 | 19008.582200 | 35.716350 | 3.943749 | 3.652794 | 4.144064 |

解释：Proposed 在 2k 有固定管理成本；10k 开始快于 Per-Agent；20k 的中位配对加速约为 3.94 倍。Simple 很快，但其准确性代价必须同时报告。

## 4. 行为分布准确性

| Scenario | Proposed median | Simple median | PerAgent median | P-S mean diff | Bootstrap low | Bootstrap high |
|---|---|---|---|---|---|---|
| None | 0.000322 | 0.781507 | 0.000000 | -0.779768 | -0.780897 | -0.778559 |
| HarvestCap | 0.000322 | 0.775135 | 0.000000 | -0.773602 | -0.774522 | -0.772588 |
| StateImport | 0.000322 | 0.785365 | 0.000000 | -0.783578 | -0.784709 | -0.782364 |
| RepairAid | 0.000432 | 0.779072 | 0.000000 | -0.778720 | -0.778782 | -0.778662 |

Proposed 的 Behavior TVD 接近 0，并且相对 Simple 的 bootstrap CI 全部远离 0。Per-Agent 在 200 人下与 Oracle 一致。

## 5. StateImport 连续性

| Metric | Proposed median | Simple median | PerAgent median | P-S mean diff | Bootstrap low | Bootstrap high |
|---|---|---|---|---|---|---|
| Continuity.PersistentIDMismatchRate | 0.000000 | 0.000000 | 0.000000 | 0.000000 | 0.000000 | 0.000000 |
| Continuity.HomeIDMismatchRate | 0.000000 | 0.000000 | 0.000000 | 0.000000 | 0.000000 | 0.000000 |
| Continuity.HomeStateMismatchRate | 0.033333 | 0.166667 | 0.000000 | -0.130000 | -0.145000 | -0.115556 |
| Continuity.MoneyMAE | 1.141667 | 2.616667 | 0.000000 | -1.496111 | -1.577222 | -1.413333 |
| Continuity.InventoryWoodMAE | 0.066667 | 0.566667 | 0.000000 | -0.473333 | -0.502222 | -0.445556 |
| Continuity.CurrentGoalMismatchRate | 0.016667 | 0.033333 | 0.000000 | -0.018333 | -0.029444 | -0.008333 |
| Continuity.FirstActionMismatchRate | 0.033333 | 0.033333 | 0.000000 | -0.011111 | -0.021111 | -0.002778 |
| Continuity.TaskActiveStatusMismatchRate | 0.908333 | 0.091667 | 0.000000 | 0.781111 | 0.741111 | 0.818889 |
| Continuity.TaskRemainingHoursMAE | 0.000000 | 0.000000 | 0.000000 | 0.675824 | 0.200000 | 1.256777 |
| Continuity.CommitmentTaskActiveStatusMismatchRate | 0.000000 | 1.000000 | 0.000000 | -0.900000 | -1.000000 | -0.766667 |
| Continuity.CommitmentTaskRemainingHoursMAE | 0.000000 | 0.000000 | 0.000000 | 1.980952 | 0.466667 | 4.000000 |

TaskRemainingHours 只在双方同时处于同类进行中任务时计算，分母很小；不能用其中位数 0 掩盖通用 TaskActive 指标越线。

## 6. 预冻结复核线

| Scenario | Metric | Threshold | Median | Max | Seeds over | Status |
|---|---|---|---|---|---|---|
| None | Continuity.MoneyMAE | 2.000000 | 1.141667 | 1.450000 | 0/30 | WITHIN |
| None | Continuity.RepairCreditMAE | 1.000000 | 0.000000 | 0.000000 | 0/30 | WITHIN |
| None | Continuity.InventoryWoodMAE | 1.000000 | 0.066667 | 0.333333 | 0/30 | WITHIN |
| None | Continuity.HomeStateMismatchRate | 0.100000 | 0.033333 | 0.083333 | 0/30 | WITHIN |
| None | Continuity.FirstActionMismatchRate | 0.100000 | 0.033333 | 0.083333 | 0/30 | WITHIN |
| None | Continuity.TaskActiveStatusMismatchRate | 0.100000 | 0.908333 | 0.966667 | 30/30 | REVIEW |
| None | Continuity.TaskRemainingHoursMAE | 4.000000 | 0.000000 | 6.000000 | 1/30 | REVIEW |
| HarvestCap | Continuity.MoneyMAE | 2.000000 | 1.141667 | 1.450000 | 0/30 | WITHIN |
| HarvestCap | Continuity.RepairCreditMAE | 1.000000 | 0.000000 | 0.000000 | 0/30 | WITHIN |
| HarvestCap | Continuity.InventoryWoodMAE | 1.000000 | 0.066667 | 0.333333 | 0/30 | WITHIN |
| HarvestCap | Continuity.HomeStateMismatchRate | 0.100000 | 0.033333 | 0.083333 | 0/30 | WITHIN |
| HarvestCap | Continuity.FirstActionMismatchRate | 0.100000 | 0.033333 | 0.083333 | 0/30 | WITHIN |
| HarvestCap | Continuity.TaskActiveStatusMismatchRate | 0.100000 | 0.908333 | 0.966667 | 30/30 | REVIEW |
| HarvestCap | Continuity.TaskRemainingHoursMAE | 4.000000 | 0.000000 | 6.000000 | 1/30 | REVIEW |
| StateImport | Continuity.MoneyMAE | 2.000000 | 1.141667 | 1.450000 | 0/30 | WITHIN |
| StateImport | Continuity.RepairCreditMAE | 1.000000 | 0.000000 | 0.000000 | 0/30 | WITHIN |
| StateImport | Continuity.InventoryWoodMAE | 1.000000 | 0.066667 | 0.333333 | 0/30 | WITHIN |
| StateImport | Continuity.HomeStateMismatchRate | 0.100000 | 0.033333 | 0.083333 | 0/30 | WITHIN |
| StateImport | Continuity.FirstActionMismatchRate | 0.100000 | 0.033333 | 0.083333 | 0/30 | WITHIN |
| StateImport | Continuity.TaskActiveStatusMismatchRate | 0.100000 | 0.908333 | 0.966667 | 30/30 | REVIEW |
| StateImport | Continuity.TaskRemainingHoursMAE | 4.000000 | 0.000000 | 6.000000 | 1/30 | REVIEW |
| RepairAid | Continuity.MoneyMAE | 2.000000 | 1.183333 | 1.466667 | 0/30 | WITHIN |
| RepairAid | Continuity.RepairCreditMAE | 1.000000 | 0.000000 | 0.066667 | 0/30 | WITHIN |
| RepairAid | Continuity.InventoryWoodMAE | 1.000000 | 0.066667 | 0.333333 | 0/30 | WITHIN |
| RepairAid | Continuity.HomeStateMismatchRate | 0.100000 | 0.033333 | 0.083333 | 0/30 | WITHIN |
| RepairAid | Continuity.FirstActionMismatchRate | 0.100000 | 0.033333 | 0.083333 | 0/30 | WITHIN |
| RepairAid | Continuity.TaskActiveStatusMismatchRate | 0.100000 | 0.908333 | 0.966667 | 30/30 | REVIEW |
| RepairAid | Continuity.TaskRemainingHoursMAE | 4.000000 | 0.000000 | 6.000000 | 1/30 | REVIEW |

`REVIEW` 表示需要解释原始明细，不表示整个方法自动失败。TaskActive 在所有场景、所有正式 Seed 越线；TaskRemainingHours 只有一个 Seed 越线。

## 7. TaskActive 原始快照分解

| Scenario | Pairs | Mismatches | Pooled rate | Median run rate | P active/O inactive | P inactive/O active | Routine/Routine | Commitment-related |
|---|---|---|---|---|---|---|---|---|
| None | 1800 | 1603 | 0.890556 | 0.908333 | 1603 | 0 | 1603 | 0 |
| HarvestCap | 1800 | 1603 | 0.890556 | 0.908333 | 1603 | 0 | 1603 | 0 |
| StateImport | 1800 | 1603 | 0.890556 | 0.908333 | 1603 | 0 | 1603 | 0 |
| RepairAid | 1800 | 1603 | 0.890556 | 0.908333 | 1603 | 0 | 1603 | 0 |

全部通用 TaskActive 不一致都来自 Proposed active / Oracle inactive。差异集中在 RoutineLife/Routine 的时间相位；同一批快照中没有 RestoreHome 承诺活动被遗漏。原始通用指标仍完整保留。

## 8. 性能执行顺序敏感性

| Population | Method | Mean schedule position | Spearman(position,total) |
|---|---|---|---|
| 2000 | Proposed | 20.700000 | 0.624242 |
| 2000 | Simple | 10.400000 | 0.357576 |
| 2000 | PerAgent | 15.400000 | 0.454545 |
| 10000 | Proposed | 15.400000 | 0.684848 |
| 10000 | Simple | 14.000000 | 0.236364 |
| 10000 | PerAgent | 17.100000 | 0.551515 |
| 20000 | Proposed | 13.000000 | 0.721212 |
| 20000 | Simple | 18.100000 | -0.321212 |
| 20000 | PerAgent | 15.400000 | 0.127273 |

正式顺序是确定性全局乱序。部分方法存在随执行位置增加而变慢的趋势；RepeatIndex 配对不能完全消除热状态和时间漂移。10k/20k 的方向与独立中位数比一致，20k 结论的余量最大。

## 9. 可信结论

1. Proposed 的固定成本使其在 2k 慢于 Per-Agent；10k 开始占优，20k 约 3.94 倍。
2. Proposed 的宏观轨迹、政策效应和行为分布误差显著低于 Simple。
3. ResidentID、HomeID 和住房承诺保持连续；普通 Routine 的精确活动相位没有保持一致。
4. Proposed 的进程峰值内存高于 Per-Agent，不能宣称内存节省。
5. 结果适用于冻结的单机、Shipping、NullRHI、StateImport 性能场景；不直接外推到所有硬件和地图。
