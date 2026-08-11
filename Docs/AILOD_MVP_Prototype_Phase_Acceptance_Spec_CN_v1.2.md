# Hierarchical AI / Simulation LOD：MVP 原型阶段验收规格

**版本：v1.2**  
**日期：2026-08-10**  
**基准文档：`AILOD_MVP_Prototype_Implementation_Spec_CN.md` v1.1**  
**用途：记录 v1.1 之后已批准的变动，并在每完成一个阶段后快速验收。本文不替代基准文档。**

## 1. 使用规则

1. 基准文档 v1.1 保留为原始实施规格和历史对照，不再随实现阶段改写。
2. 本文只记录已批准的差异、阶段状态、Definition of Done（DoD）证据和验收结论。
3. 每次只实施一个阶段；当前阶段通过验收并由项目作者确认后，才进入下一阶段。
4. 未经确认，不改变 `[FROZEN]` 参数、公式、方法边界或日志字段，也不加入 LLM、PCG、社交系统或额外经济复杂度。
5. C++ 负责确定性研究逻辑；Blueprint 只负责配置和演示表现。

## 2. v1.2 已批准变动

以下内容覆盖基准文档 v1.1 中对应条目，其余规则继续以 v1.1 为准。

| 项目 | v1.1 | v1.2 已批准值 |
|---|---|---|
| 初始金币 | Low `[0,4)`；NonLow `[4,8)` | 只用整数：Low `{0,1,2,3}`；NonLow `{4,5,6,7}`，稳定均匀分配 |
| 木材付款 | 未冻结取整方法 | 每笔交易只对总价取整一次：`PaymentCoins(q)=ceil(q×P)`；不得先对单价取整；BuyWood 扣除此整数金额 |
| 初始个体状态 | 未逐项固定 | Wood=0、RepairCredit=0、地震前 HomeState=Healthy、无进行中 Event、ArriveID=0 |
| Repair Aid 资格 | `Cash + RepairCredit < 4P` | 使用实际整数付款额：`Cash + RepairCredit < ceil(4P)` |
| 稀缺资源竞争 | 未定义统一先后号 | 请求获得唯一递增 `ArriveID`，编号小者优先；LOD 切换保留编号；取消后重新申请才取新号；群体请求使用批次号 |
| ArriveID 持久化与日志 | 无此字段 | 加入 Event/CoreState、Macro↔Micro 转换和 EventBatchRef；`simulation_events`、`lod_transitions`、`ledger_transactions` 记录该字段 |
| A 国 N=100 地震损伤 | 要求各分层精确 30%，但人数无法全部整除 | Logger Low 4、Logger NonLow 2、Worker Low 17、Worker NonLow 7，共 30 人 |
| 大规模主压力场景 | 建议 Earthquake + StateImport，正式前冻结 | 冻结为 Earthquake + StateImport |
| 性能环境 | 待记录 | 当前机器：i7-14650HX（16C/24T）、RTX 4050 Laptop、约 16 GB RAM、Windows 11 Pro 22621；接通电源并使用高性能模式 |
| 构建用途 | 待记录 | Shipping 为正式性能报告；Development 仅用于开发、诊断和补充对照 |
| 批量准确性运行 | 待确认 | UE 内无渲染运行并复用同一套无 Actor C++ 核心，不另写第二套模拟 |
| 望远镜演示 | 待确认 | 阶段 7 保留有画面的固定地图演示，用于直观观察 LOD 切换 |

继承 v1.1 的性能规模：`2k / 10k / 20k`，3 种方法，每配置 10 次，共 90 Runs。此项不是 v1.2 新改动。

## 3. 阶段总览

| 阶段 | 目标 | 状态 | 验收日期 |
|---:|---|---|---|
| 0 | 冻结数据契约 | 实现完成；DoD 自动验收通过，待作者确认 | 2026-08-10 |
| 1 | 纯数据模拟核心 | 未开始 | — |
| 2 | 王国与 Structured Macro | 未开始 | — |
| 3 | 最小 Utility + GOAP 与 Oracle | 未开始 | — |
| 4 | Macro–Micro Bridge 与 Persistent | 未开始 | — |
| 5 | Simple Aggregate 与 Per-Agent 两个 Baseline | 未开始 | — |
| 6 | Experiment Runner 与离线指标 | 未开始 | — |
| 7 | UE5.4 表现、Criticality 与望远镜演示 | 未开始 | — |
| 8 | Pilot 与正式冻结 | 未开始 | — |

## 4. 阶段 0 验收记录

**范围：** 只建立 Phase 0 数据类型、日志字段常量、确定性初始 Population Manifest、地震损伤清单和自动化测试；没有提前实现阶段 1 的时钟、调度器或 Ledger。

**DoD 证据：**

- UE 5.4 Editor target 构建成功。
- 自动化测试 `AILODResearch.Phase0.ManifestDeterminism`：`Success`。
- 同一配置与 Seed 连续生成 RunA/RunB，人口清单和地震损伤清单逐字节一致。
- 小规模清单共 200 人，A/B 各 100；每国四个 BaseCohort 人数为 `14 / 6 / 56 / 24`。
- A 国受损人数为 `4 / 2 / 17 / 7`，合计 30。
- 冻结规模 `2k / 10k / 20k` 均可生成正确总人口和 A 国 30% 受损人数。
- Population Manifest SHA-256：`A66378CC7A1EDF70374385BA4E3BB8BCBECD7694B7CBD137CD4050AC6A69A3EC`。
- Earthquake Damage List SHA-256：`0B7E0C8BFF9D8714DDCD98DDFB4911B5DCBB3DC960D6F7E6366503751CF9461E`。

**验收结论：** 阶段 0 的实现与自动化 DoD 已通过，等待项目作者最终确认。阶段 1 尚未开始。

## 5. 后续阶段验收模板

每完成一个阶段，在本文新增一节并填写：

- 阶段、日期、状态；
- 本阶段实际实施范围；
- 与 v1.2 相比是否有经批准的变动；
- 该阶段 DoD 逐项结果；
- 构建与自动化测试结果；
- 输出文件、日志位置和必要 Hash；
- 已知限制或失败项；
- 项目作者验收结论，以及是否批准进入下一阶段。

## 6. 版本记录

| 版本 | 日期 | 内容 |
|---|---|---|
| v1.2 | 2026-08-10 | 从 v1.1 主规格拆出已批准变动与阶段验收记录；登记阶段 0 DoD 证据。 |
