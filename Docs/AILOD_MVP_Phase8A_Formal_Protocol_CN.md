# AILOD MVP Phase 8-A 正式实验冻结协议

**日期：2026-08-31**

**分支：`phase-8-formal-experiments`**

**基线提交：`33140342cdfe0b02c0bb003e401424011dbf08f1`**

**范围：只关闭正式实验入口、运行协议和输出审计；不修改 v1.9 模拟行为。**

## 1. 大白话说明

Phase 8-A 先保证实验“能从正确的 Shipping 程序启动、不会少跑或串轮次、跑完能自动检查”。本阶段的预检数据只用来检查管线，整批提前列入排除清单，不进入论文结果。

## 2. 当前冻结的正式矩阵

### 2.1 准确性

- 每国 100 人，总人口 200；
- Oracle、Proposed、Simple、Per-Agent；
- None、HarvestCap、StateImport、RepairAid；
- 30 个配对 Seed：整数标识 `20260815` 到 `20260844`；
- 每个组合 1 次，共 480 Runs；
- `OrderSeed=830480`；
- 输出根：`D:\AILODFormal\Phase8\FormalAccuracy-v1`。

这 30 个 Seed 与 H5/H6 使用的 `20260810—20260814` 分开，避免继续用已经看过的 Pilot 数据调整结论。

### 2.2 性能

- 每国人口分别为 1000、5000、10000，对应总人口 2k、10k、20k；
- Proposed、Simple、Per-Agent；
- 固定 StateImport。Day 0 地震属于所有场景的共同输入，所以这里等价于冻结的 Earthquake + StateImport 主压力场景；
- 固定 Seed `20260815`，每种方法重复 10 次；
- 每个人口档 30 Runs，合计 90 Runs；
- 2k/10k/20k 的 `OrderSeed` 分别为 `830002 / 830010 / 830020`；
- 三个人口档分开保存和汇总，只在相同人口档、相同重复编号内计算相对 Per-Agent 的速度；
- 输出根：`D:\AILODFormal\Phase8\FormalPerformance-v1\N2000/N10000/N20000`。

性能主指标使用 Day -7 到 Day 60 全部 `ai_cpu_ms` 之和。约一秒采样桶的 Mean/P95/P99 用来描述运行波动，不代替整场速度比。

## 3. 正式运行环境

- UE 5.4.4；
- `AILODResearch Win64 Shipping`；
- Shipping Game 通过 `-run=AILODExperiment` 启动；
- NullRHI、无声音、无人值守；
- 同一台机器、接通电源、高性能电源方案；
- 正式输出写入 D 盘；
- 每次启动前，脚本要求当前 HEAD 为完整 40 位提交号，并要求已跟踪工作树和暂存区干净；
- 现有未跟踪论文材料不进入实验输入，也不由脚本处理。

## 4. 正式资格硬门

每份 Run 必须同时满足：

- `valid=true`；
- `formal_model_eligible=true`；
- `formal_run_requested=true`；
- `formal_environment_eligible=true`；
- `valid_for_formal_experiment=true`；
- `formal_eligibility_reason=eligible`；
- `build_type=Shipping`；
- Git、硬件、输入 Hash、OrderSeed、ScheduleIndex 和 RepeatIndex 与冻结协议相符；
- `hard_errors` 中全部字段为 0；
- Proposed 使用 `spec_version=1.9` 和 `deterministic_digest_version=1.9-domain-v1`；
- 原始文件、汇总文件和运行数量完整；
- 没有 `run_failures.csv`。

`Tools/Phase8/Test-Phase8Output.ps1` 负责逐项检查。任何一项失败，整批停止，不进入统计。

## 5. 永久排除的数据

机器可读排除清单位于 `Tools/Phase8/Phase8-Exclusions.json`。至少包括：

- H5 Development Pilot；
- H6 Development Pilot；
- Phase 7F 有画面 Development 数据；
- Phase 8-A Accuracy Preflight；
- Phase 8-A Performance Preflight。

Phase 8-A 预检会显式使用 `-Formal`，因为只有这样才能验证 Shipping 正式资格门。它们在运行前已经写入排除清单，所以即使 Manifest 显示正式资格通过，也不能进入论文统计。

## 6. Phase 8-A 预检矩阵

- Accuracy：4 方法 × None/StateImport × Seed `20260810`，共 8 Runs；
- Performance：3 方法 × StateImport × Seed `20260810` × 2 次重复，共 6 Runs；
- 合计 14 Runs；
- Accuracy `OrderSeed=830801`；Performance `OrderSeed=830802`。

预检必须证明：

1. Shipping Game 能运行非 Editor Commandlet；
2. 14 份输出全部通过正式资格和零硬错误门；
3. Performance 的 R01 只配 R01 Per-Agent，R02 只配 R02；
4. Resume 能完整跳过已经完成的 Run；
5. 删除预检的 `metrics_summary.csv` 后，可以只用原始输出重建相同 Hash；
6. 两份预检审计成功后，正式运行脚本才允许启动 480/90 矩阵。

## 7. 运行入口

构建 Shipping：

```powershell
& 'D:\ruanjian\Unreal Engine\UE_5.4\Engine\Build\BatchFiles\Build.bat' AILODResearch Win64 Shipping 'C:\WarwickProjects\AILODResearch\AILODResearch.uproject' -WaitMutex -NoHotReload
```

预检和续跑统一通过：

```powershell
Tools\Phase8\Invoke-Phase8Experiment.ps1 -RunSet PreflightAccuracy
Tools\Phase8\Invoke-Phase8Experiment.ps1 -RunSet PreflightPerformance
```

正式矩阵只有在 Phase 8-A 预检通过并获得项目作者单独批准后才能启动。

## 8. 冻结边界

正式数据开始后：

- 不修改 v1.9 行为、Seed、场景、人口、方法、重复数或 Activation Trace；
- 不修改原始日志 Schema、主要指标和配对方法；
- 不用相机、望远镜、Actor、PCG 或 Demo 命令选择正式样本；
- 不把 Preflight、Pilot、Development 或 Phase 7F 数据混入正式统计；
- 失败只记录、定位和从同一冻结提交续跑。需要修改代码时，当前正式批次整体作废并重新冻结版本。
