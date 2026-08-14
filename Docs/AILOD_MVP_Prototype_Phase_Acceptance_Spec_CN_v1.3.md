# Hierarchical AI / Simulation LOD：MVP v1.3 冻结与阶段验收规格

**版本：v1.3**
**日期：2026-08-14**
**状态：MVP 范围已由项目作者批准**
**基准文档：`AILOD_MVP_Prototype_Implementation_Spec_CN.md` v1.1**
**前序变动：`AILOD_MVP_Prototype_Phase_Acceptance_Spec_CN_v1.2.md`**
**用途：冻结第一版可验证 MVP；记录阶段 0 补充项和阶段 0—8 快速验收门槛。本文不修改或替代历史文档。**

## 1. 文档优先级

当前实现依次读取 v1.1 基准规格、v1.2 已批准变动和本文 v1.3。若同一条目冲突，以较新的版本为准；未被本文改变的内容继续继承 v1.1/v1.2。

每次只实施一个阶段。当前阶段通过 Definition of Done（DoD）并由项目作者确认后，才进入下一阶段。未经确认，不改变 `[FROZEN]` 参数、公式、方法边界或日志字段。

## 2. v1.3 MVP 研究边界 `[FROZEN]`

MVP 研究目标是验证 Structured Macro Simulation 是否能在固定 50 个 Active Micro NPC 的预算下，同时保持可扩展性、宏观结果近似和 Persistent 居民跨 LOD 连续性。MVP 不是完整社会或经济游戏。

### 2.1 MVP 必须包含

- UE 5.4；C++ 负责确定性研究逻辑，Blueprint 只负责配置和表现。
- 两个王国 A/B；A 国发生地震，B 国保持未受灾对照。
- 唯一核心资源 Wood，以及既有 Coin、Treasury、Market、Home Repair 状态。
- 固定政策场景 `None / HarvestCap / StateImport / RepairAid`；正式实验中不让国王自由选择政策。
- 四种方法：Oracle、Simple Aggregate、Per-Agent、Proposed Structured Macro。
- 小规模总人口 200；大规模总人口 `2k / 10k / 20k`；全局 Active Micro Cap=50。
- Clock、Scheduler、Ledger、Reservation、Event Store、Cohort、Utility/GOAP、Macro↔Micro Bridge、Persistent CoreState、Experiment Runner 和离线指标。
- 固定灰盒地图、最小只读数据 UI、望远镜观察、Persistent 居民固定身份对话。
- Persistent 连续性使用既有 Repair Event 作为主要任务载体，不等待跨国运输功能。

### 2.2 MVP 最小可视化

- 地图只需要 A/B 两个区域，以及 Forest、Market、Home、Work 固定 Anchor。
- 居民使用 UE 现有简单角色资源；不制作定制人物或复杂动画。
- UI 只读显示：权威时间、方法、场景、人口、Active Micro、两国 Wood Stocks、Coin/Treasury、WoodPrice、HomeState totals 和最近 Ledger 事务。
- 望远镜演示可以根据观察提高居民 Criticality；正式比较仍使用固定 Activation Trace。MVP 中 `PlayerTask` 输入保持为 0。
- 固定身份对话只读取 C++ CoreState：Name、Kingdom、Profession、HomeState、CurrentGoal、CurrentEvent/Progress。不得由 UI 缓存或自行计算权威状态。
- 开关地图表现、UI、望远镜和对话不得改变同一 Seed 的模拟日志。

## 3. MVP 正式验证矩阵 `[FROZEN]`

| 验证 | 人口/方法 | 配置与 Runs | 回答的问题 |
|---|---|---|---|
| A. 小规模准确性 | 200；Oracle、Simple、Per-Agent、Proposed | 4 场景 × 4 方法 × 30 paired Seeds = 480 正式 Runs | Cohort/低频/简单聚合相对详细个体参考产生多少宏观与行为误差 |
| B. 大规模性能 | 2k、10k、20k；Simple、Per-Agent、Proposed | 3 规模 × 3 方法 × 10 次 = 90 正式 Runs | CPU、内存和激活/转换成本如何随规模增长 |
| C. Persistent 连续性 | 复用固定 Persistent Pool 和激活时间表 | 复用 A/B 的原始 Run，不单独增加统计 Run | 身份、资源、Repair Event 和首次行为是否跨 LOD 连续 |

MVP 完成不要求已经采集完 570 个正式 Runs。MVP 的实验门槛是：Runner、日志、离线指标和 5 个 Pilot Seeds 能完整运行并冻结配置。570 个正式 Runs 属于 MVP 完成后的论文数据采集。

Oracle 只在 200 人规模提供详细个体参考；它不证明 20k 结果绝对正确。20k 额外依赖守恒、幂等、连续性和跨规模趋势检查。

## 4. 明确推迟到 MVP 之后 `[OUT OF MVP]`

以下内容不得在阶段 1—8 中顺便加入：

- 玩家影响国王的动态决策或闭环 King Utility；
- 玩家给居民派发任务；
- 跨国 Shipment、A→B 木材运输或双向贸易；
- Food、Food Shortage、饥饿、死亡、迁移或食物价格；
- A/B 同时遭遇不同灾害；
- LLM、自由对话、好感度、完整社交系统；
- PCG 城市、多商品经济、复杂国王人格；
- 20,000 个同时可见 Actor；
- 定制人物、复杂建筑和非研究必要的美术内容。

## 5. 阶段 0 v1.3 补充数据契约

v1.2 的确定性 Population Manifest 与 Earthquake Damage List DoD 继续有效。由于 MVP 新增 Persistent 身份对话，v1.3 在进入阶段 1 前必须补充以下内容。

### 5.1 Persistent 测试池 `[FROZEN]`

- 每个 Run 使用一个全局 20 人的 Persistent 测试池。
- Day 7 激活其中固定 10 人；Day 30 再激活同一 10 人；Day 45 同时请求激活全部 20 人。
- 20 人必须通过独立稳定随机子流从 Initial Population Manifest 确定性分层选择，并在所有适用方法中共享同一身份清单。
- 被选居民在 Initial Population Manifest 中获得非零 `PersistentID`；匿名居民保持 0。
- 每个 Persistent 居民获得确定性 `Name`。Name 只用于身份与表现，不加入 Cohort Key，也不影响 Utility/GOAP。

### 5.2 ID、随机子流与日志契约 `[FROZEN]`

- 阶段 0 明确定义 Resident、Home、Persistent、Event、Arrive、Transaction、Reservation、Policy 标识符及 `IdempotencyKey` 的稳定格式。
- 冻结并集中登记至少四个随机子流：Population Composition、Initial Cash、Earthquake Damage、Persistent Selection；不同系统不得复用同一子流。
- `AILODLogSchema` 必须为每种日志冻结字段名和类型，而不只保存文件名。
- 运行前共享输入 `initial_population_manifest.json` 与 `earthquake_damage_list.json` 只要求 `schema_version / spec_version / seed / config_hash` 及其内容字段；它们不要求 `method / run_id / game_time`。
- 每个正式 Run 的 `run_manifest.json` 引用上述输入文件 Hash，并记录 `experiment_id / run_id / method / scenario / seed / Git commit / UE version / build / hardware`。
- 所有运行时日志继续包含 v1.1/v1.2 已冻结的公共字段；任何例外必须在本文新增决策记录。

### 5.3 阶段 0 v1.3 DoD

- 同一配置与 Seed 两次生成的 Population、Damage 和 Persistent 清单逐字节一致。
- 200、2k、10k、20k 总人口与 A 国损伤数量正确。
- Persistent Pool 恰好 20 人；Day 7/30 身份清单相同；Day 45 包含全部 20 人。
- PersistentID 非零且唯一；Name 稳定且与身份绑定；匿名居民 PersistentID=0。
- 生成的 JSON 可以解析；`config_hash` 与文件 SHA-256 可重算一致。
- 自动化测试验证每类日志的冻结字段表，没有实现 Clock、Ledger、Cohort、GOAP 或 Actor。

## 6. 阶段 0—8 状态与快速验收

| 阶段 | MVP 范围 | 当前状态 | DoD 摘要 |
|---:|---|---|---|
| 0 | 数据契约、初始化、Persistent Pool、日志 Schema | **v1.3 已通过（2026-08-14）** | 第 5.3 节全部通过 |
| 1 | 纯数据模拟核心 | **v1.3 已通过（2026-08-14）** | 空场景运行 60 日；人口/木材残差和重复事务为 0 |
| 2 | 王国与 Structured Macro | **v1.3 已通过（2026-08-14）** | 四固定政策场景运行 60 日；无负库存；每次 Stock 变化可解释 |
| 3 | 最小 Utility + GOAP 与 Oracle | 未开始 | Work/Buy/Chop/Repair/Wait 可执行；所有资源经过 Ledger |
| 4 | Macro–Micro Bridge 与 Persistent | 未开始 | Repair 50% 时切换两次不重置、不重复扣木；Day 7/30 同一身份连续 |
| 5 | Simple 与 Per-Agent Baseline | 未开始 | 三个可部署方法共享 Scenario/Domain/日志；方法边界无偷渡 |
| 6 | Experiment Runner 与离线指标 | 未开始 | Run 可由 Manifest 复现；删除 summary 后可从原始日志重建 |
| 7 | 固定地图、UI、望远镜、固定身份对话 | 未开始 | 表现层开关不改变日志；UI 等于 CoreState；Active Micro≤50 |
| 8 | Pilot 与正式冻结 | 未开始 | 5 Pilot Seeds、配置 Hash、排除清单、硬件/构建和正式脚本冻结 |

阶段 1 只有在阶段 0 v1.3 补充 DoD 通过并由项目作者确认后才能开始。

### 6.1 阶段 0 v1.3 实施与验收记录（2026-08-14）

- Persistent Pool 固定为全局 20 人，A/B 各 10 人；每国按初始人口比例使用最大余数法分配为 Logger Low 1、Logger NonLow 1、Worker Low 6、Worker NonLow 2。
- Day 7 与 Day 30 在每国使用同一 5 人，合计同一 10 人；Day 45 使用池内全部 20 人。产物显式记录 `day7 / day30 / day45`，不依赖隐含解释。
- `Name` 使用与 ResidentID 绑定的稳定格式 `Resident-%06lld`，只供身份与表现使用；匿名居民 Name 为空且 PersistentID=0。
- 四条独立随机子流已集中登记：Population Composition、Initial Cash、Earthquake Damage、Persistent Selection。
- 日志 Schema 已冻结字段名与类型；阶段 0 只生成 Population、Damage、Persistent Pool 三份运行前输入，不生成阶段 1 的 Clock、Ledger、Cohort、GOAP 或 Actor。
- UE5.4 Development Editor 编译：通过。
- 自动化测试 `AILODResearch.Phase0.LogSchema`：Success。
- 自动化测试 `AILODResearch.Phase0.ManifestDeterminism`：Success。
- 外部 JSON 复核：Population=200、A 国 Damage=30、Persistent=20、Day 7=10、Day 30=10 且名单相同、Day 45=20、A/B 各 10。
- 配置 Hash（稳定 SHA-1）：`C5CA7EE7F18DA153A93E345FF3FF5EFC9695568A`。
- `initial_population_manifest.json` SHA-256：`D4B9458E2A09BE35A114C809F59D25B0F2355517C40F3C6154BFE01CCA3E517C`。
- `earthquake_damage_list.json` SHA-256：`01E31757183749BEBA05FCC7035021BA854B1496409A240B42994C7C24F31204`。
- `persistent_test_pool.json` SHA-256：`7E64BC2C8B356A1BD0024F2AD6E7E93E5EAC89BDA7E825838F75959264297B8A`。
- RunA/RunB 对应文件 SHA-256 逐项相同；200、2k、10k、20k 生成规模均由自动化测试覆盖。

**结论：阶段 0 v1.3 补充 DoD 已通过；项目作者随后已批准进入阶段 1。**

### 6.2 阶段 1 实施与验收记录（2026-08-14）

- 实现无 Actor C++ 纯数据核心：Authoritative Clock、Scheduler、Ledger、Reservation Store、Event Store 与 Conservation Audit。
- 权威时间使用整数游戏分钟；正式离屏推进仍固定为每步 1 游戏小时，因此既能稳定运行 1,440 个小时步，也能精确表示 `D00T12:17` 等中途观察点。
- Scheduler 按 `ExecuteTime → ArriveID → EventID` 稳定排序；同一时间的稀缺资源请求由较小 ArriveID 先处理。
- Ledger 支持 Wood 与整数 Coin；所有内部转移和 Boundary Flow 生成唯一 TransactionID，并以 IdempotencyKey 阻止重复提交；任何会产生负库存的事务均被拒绝。
- Reservation 的创建、提交和释放全部通过 Ledger 转移，不直接改 Stock；Event Store 为每个 EventID 只保存一个当前 Owner，并阻止错误 Owner 转移和重复完成。
- 空场景分别使用总人口 `200 / 2k / 10k / 20k`，每个规模均从 Day 0 逐小时推进至 `D60T00:00`，共 1,440 步。
- 四个规模的验收结果均为：PopulationResidual=0、WoodResidual=0、DuplicateTransaction=0、NegativeStock=0，全部硬错误门通过。
- UE5.4 Development Editor 编译：通过，无阶段 1 编译警告。
- 自动化测试 `AILODResearch.Phase1.CoreContracts`：Success。
- 自动化测试 `AILODResearch.Phase1.EmptyScenario60Days`：Success。
- 本阶段没有实现 Cohort、森林增长、市场价格、地震、政策、维修、GOAP、LOD 转换、Actor 或 UI；这些仍分别属于阶段 2 及以后。

**结论：阶段 1 DoD 已通过；项目作者随后已批准进入阶段 2。**

### 6.3 阶段 2 已批准执行顺序与边界（2026-08-14）

- Baseline External Import 固定为每小时按 `0.02N wood/day` 从 `ExternalBoundary → MarketWoodAvailable`，立即到账、无国库费用、无运输延迟；StateImport 仍单独进入 `WoodInTransit` 并延迟 3 游戏日。
- Routine Boundary Consumption 固定为每小时按 `0.10N wood/day` 从 `MarketWoodAvailable → ExternalBoundary`；库存不足时只扣实际可用量并记录 unmet quantity，不从 Forest、维修材料或居民库存补扣。
- 每小时顺序固定为：到期事件/进口到货 → 当日政策事件 → 森林增长 → Baseline Import → 商业采伐 → Routine Consumption → 木价更新 → 守恒检查与日志。
- Day 0 地震发生在首个正式小时 Flow 之前；StateImport 在 Day 2—14 的每日零点最多下单一次，到货在同小时其他 Flow 之前处理。
- A 国接收地震与场景政策，B 国保持无灾害、无政策的基线对照。
- 阶段 2 的 Repair Aid 只转移 Treasury Coin 并更新 Cohort RepairCredit；Work、BuyWood、ChopWood、Repair、Wait 和实际房屋维修属于阶段 3。

上述规则由项目作者明确批准。其基线关系为：初始森林 Growth≈`0.08N/day` 抵消 Baseline Harvest=`0.08N/day`；Market 的 Harvest `0.08N/day` + Baseline Import `0.02N/day` 抵消 Routine Consumption `0.10N/day`。

### 6.4 阶段 2 实施与验收记录（2026-08-14）

- 实现两个王国的稀疏 `BaseCohort × HomeState × MacroIntent` 容器及第 4.3 节最小字段；非空桶数量不超过冻结上限 192。
- 实现 7 日预热 + 60 日正式时间线，共 1,608 个小时步；Day 0 使用 Phase 0 Damage List 将 A 国 `4 / 2 / 17 / 7` 户转为 DamagedWaiting，B 国 100 户保持 Healthy。
- 实现 Logistic Forest Growth、Baseline Harvest、Baseline External Import、Routine Boundary Consumption、WoodPrice 延迟调整和四个单独场景 `None / HarvestCap / StateImport / RepairAid`。
- HarvestCap 在 `[Day 3, Day 30)` 只限制 A 国商业采伐；StateImport 使用 `1.25 coin/wood`、`0.08N/day` Cap、`1.0N coin` Budget 和 3 日延迟；RepairAid Day 2 计算资格、Day 3 按 ArriveID 批次顺序支付整数 Coin。
- StateImport 下单时通过 Reservation 将 TreasuryAvailable 转为 TreasuryReserved，并把外部 Wood 加入 WoodInTransit；到货时先把 Wood 转入 Market，再将 Reserved Coin 支付到 ExternalBoundary，Day 60 无未结算预留。
- 所有 Wood/Coin Stock 改变均通过共享 Ledger；JSONL 每行包含冻结公共字段、GameTime、TransactionID、IdempotencyKey、ArriveID、Source、Destination、Quantity、BoundaryFlag、EventID 和 PolicyID。
- 四场景均完成 `D60T00:00`：PopulationResidual=0、WoodResidual=0、DuplicateTransaction=0、NegativeStock=0、EventOwnerConflict=0、DuplicateCompletion=0。
- `None`：A Forest=`1600`、Market=`200`、WoodPrice=`1.0`、Treasury=`500`；Digest=`26BB349E4B99500CC752C34BAFBA63A459D3F5B7`。
- `HarvestCap`：A Forest=`1627.965041`、Market=`146`、WoodPrice=`1.170411`、Treasury=`500`；Digest=`04D0F4E12B20E875BB97B20D3D4CF008CC3FAF28`。
- `StateImport`：额外进口 80 Wood；A Forest=`1600`、Market=`280`、WoodPrice=`0.845154`、Treasury=`400`、InTransit=`0`；Digest=`5ECAA91B38B372DB392B9A7C7B65D11FB47E1867`。
- `RepairAid`：21 户符合资格，预算按 ArriveID 顺序支付 20 户、共 40 Coin；A Market=`200`、Treasury=`460`、ResidentRepairCredit=`40`；Digest=`B11B9171D51A9EEAB1E0B4E5B109D808E12F9AAB`。
- 20k StateImport 额外验收：A 国 3,000 户受损、人口与木材残差为 0、无硬错误；Digest=`B620C75FA44D4A3F453AD1C21D680ABDEDED9F34`。
- UE5.4 Development Editor 编译：通过。
- 自动化测试 `AILODResearch.Phase2.FourScenarios60Days`：Success；验证四场景、小时顺序、Aid ArriveID 分配、JSON 解析和逐事务 Ledger 重放。
- 自动化测试 `AILODResearch.Phase2.DeterminismAndScale`：Success；同配置/Seed Digest 与 Ledger JSONL 一致，并验证 20k StateImport。
- `None` Ledger SHA-256：`D5232DBFC615E6D3F2536AEAA0B7D045A474936E422ED1CB741317AFC6504243`。
- `HarvestCap` Ledger SHA-256：`43A6E752EF98A3D2DA339A73E86DF3AD9E4B6F3D450439EF64D07E91287FAAD2`。
- `StateImport` Ledger SHA-256：`4CDE01B7BA4252DC560867188067CB19D168247A00C74F7BB70840A61F7A9057`。
- `RepairAid` Ledger SHA-256：`55FF0C0D670F95CC0F638D1066779563937784A731E27E806A50CD66832FAE06`。

阶段 2 的政策轨迹只证明 Stock/Flow、政策时间线和事务机制按当前合成规则运行；因为尚无居民行为和维修，它们不是论文最终准确性结果。

**结论：阶段 2 DoD 已通过；阶段 3 尚未开始，等待项目作者确认进入。**

## 7. MVP 总体验收

MVP 必须同时满足：

1. 所有研究逻辑由同一套无 Actor C++ 核心驱动。
2. 同 Seed 复现初始化、地震、政策、行为和原始日志。
3. 人口、木材、事件 Owner、Reservation 和事务幂等硬错误为 0。
4. Oracle、Simple、Per-Agent、Proposed 的方法边界符合 v1.1 第 21 节。
5. 200 人 Runner 能执行全部四种方法和四个固定政策场景。
6. 2k/10k/20k Runner 能执行 Simple、Per-Agent、Proposed。
7. Proposed 的 Persistent 居民在 Macro↔Micro 后 Name/ID/Home/Profession 不变。
8. 未完成 Repair Event 保留 EventID、ReservationID 和 RemainingWork；木材只结算一次。
9. 固定地图、最小 UI、望远镜和固定身份对话可演示，且不成为权威状态。
10. 5 个 Pilot Seeds 完成，所有正式配置和日志 Schema 冻结。

## 8. MVP 后扩展顺序

| 顺序 | 扩展 | 正面影响 | 负面影响/前置条件 |
|---:|---|---|---|
| 1 | UI 折线图、筛选、头顶名称、丰富固定对话、演示模式选择既有政策 | 提升答辩可读性，不改变核心模型 | 增加 UI/Blueprint 工作；不得影响正式 Run |
| 2 | 玩家派发单向 Wood Shipment；Persistent NPC 作为批事件负责人 | 直观连接玩家、个体任务与宏观资源 | 需要 Task/Shipment ID、跨国 Ledger、配对测试和新的版本决策 |
| 3 | 最小王国级 FoodStock 与 Food Shortage；Wood/Food 双向 Shipment | 支持双灾害互助叙事 | 必须重新审查对照、四方法、Cohort、GOAP、日志和实验因果 |
| 4 | 玩家建议影响固定 King Utility，并在后续决策点读取新状态 | 形成真正闭环反馈演示 | 必须冻结输入 Trace、决策周期、冷却和配对 Control；不得直接写成现实政策正确性 |

LLM、完整社交、个人饥饿/死亡、PCG、多商品经济和复杂政治系统不在当前计划中。

## 9. v1.3 决策记录

| 版本 | 日期 | 决定 | 原因与影响 | 批准人 |
|---|---|---|---|---|
| v1.3 | 2026-08-14 | MVP 保留木材/地震/固定政策/四方法；加入最小地图、UI、望远镜、Name 与固定身份对话；推迟动态国王、玩家任务、跨国贸易和 Food | 先验证 Simulation LOD 的准确性、性能与连续性，避免玩法扩张破坏对照和导致阶段 1 后返工；v1.2 阶段 0 基础结果保留，但需完成第 5 节补充 DoD | 项目作者 |
| v1.3 Phase 2 | 2026-08-14 | 冻结 Baseline Import 立即进入 Market、Routine Consumption 只从 Market 扣除，以及“到期事件 → 政策 → Growth → Baseline Import → Harvest → Consumption → Price → Audit/Log”的小时顺序 | 使 None 从设计平衡点出发，避免隐含来源或执行顺序改变政策轨迹；阶段 2 不提前实现居民行为与维修 | 项目作者 |
