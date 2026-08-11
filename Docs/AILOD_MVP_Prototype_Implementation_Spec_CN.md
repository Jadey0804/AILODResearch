# Hierarchical AI / Simulation LOD：MVP 原型实施规格

**面向 UE5.4 项目 `AILODResearch` 的研究设计、数据契约与实验协议**  
版本：v1.1（原型冻结候选）  
日期：2026-08-10  
用途：交给导师快速审阅，也作为新 Codex 会话实现项目原型的单一事实源。

---

## 0. 如何使用这份文档

### 0.1 给新 Codex 会话的建议开场提示词

```text
请完整阅读附件《Hierarchical AI / Simulation LOD：MVP 原型实施规格》。
它是当前原型的单一事实源。先检查现有 UE5.4 项目、Source 结构和未提交改动，
再把文档要求拆成阶段 0—8 的可验证实施计划。

开始写代码前：
1. 列出文档与现有项目之间的冲突；
2. 只询问会实质改变模型或实验的未决问题；
3. 不自行改变 [FROZEN] 参数、公式、方法边界和日志字段；
4. 未经确认，不加入 LLM、PCG、社交系统或额外经济复杂度；
5. C++ 负责确定性研究逻辑，Blueprint 只负责配置和演示表现；
6. 每次只实施一个阶段，并用该阶段的 Definition of Done 验证。
```

### 0.2 标注规则

| 标记 | 含义 | 使用原则 |
|---|---|---|
| `[LIT]` | 文献支持的概念、方法或设计边界 | 必须能追到文末参考文献 |
| `[ADAPTED]` | 从文献思想改编，但公式或实现不是原文直接给出 | 同时说明改编之处 |
| `[MVP]` | 本项目为了可实现、可答辩而设定的模型规则或参数 | 不得写成现实规律 |
| `[TEST]` | 本项目对研究问题的操作化、指标或实验程序 | 正式实验前冻结 |
| `[FROZEN]` | 当前已接受，新会话不得自行改变 | 如需改变必须先讨论并记录 |
| `[PILOT]` | 只允许用预实验校准一次 | 正式实验开始后不得追着结果调参 |
| `[OPEN]` | 实现前仍需记录或确认 | 不允许静默猜测 |

### 0.3 一句话理解项目

玩家附近只运行少量完整 NPC；玩家不在时，不冻结整个王国，也不让 20,000 人继续逐个思考，而是用带有 Cohort、分布、事件进度和资源账本的 Structured Macro 推进社会状态；NPC 再次被观察时，从宏观状态恢复成连续、合理且资源守恒的个体。

---

## 1. 研究目标与范围

### 1.1 研究问题

> 在固定 50 个详细 NPC 的预算下，Structured Macro Simulation 能否以低于 Per-Agent Update LOD 的计算成本，把固定的国王政策和王国事件传播给离屏人口，并使居民恢复微观模拟时的状态与行为，比 Simple Aggregate 更接近小规模 Detailed Individual Oracle？

### 1.2 子问题

1. **RQ1 — 性能：** 人口从 2,000 扩展到 20,000 时，各离屏方法的 AI CPU、内存和激活延迟怎样增长？
2. **RQ2 — 宏观准确性：** Structured Macro 的人口状态、资源轨迹和政策效应，与小规模 Oracle 的差距有多大？
3. **RQ3 — 行为一致性：** 宏观状态恢复成 NPC 后，Utility/GOAP 的首个目标、行动和群体行为分布是否合理？
4. **RQ4 — 跨 LOD 连续性：** 身份、住所、任务、事件进度和资源事务在离开、返回与事件中途切换时能否连续？

### 1.3 假设

- **H1：** 在大规模人口下，Proposed Structured Macro 的 P95 AI CPU 时间显著低于 Per-Agent Update LOD。
- **H2：** 在小规模对照中，Proposed 的状态轨迹和政策效应误差低于 Simple Aggregate。
- **H3：** Proposed 的人口、木材、事件所有权和事务错误为零，并能维持预先指定 Persistent NPC 的强连续性。

### 1.4 目标与非目标

**目标：**

- 在 UE5.4 中实现可复现的 AI/Simulation LOD 原型；
- 用最多 20,000 个逻辑居民、全局最多 50 个 Active Micro NPC 演示可扩展性；
- 完整走通“地震/政策 → 离屏人口 → 激活后的 Utility/GOAP → 写回宏观”链路；
- 生成可复算的日志，并把准确性、连续性和性能分开测量。

**非目标：**

- 不制作完整游戏，也不声称建立了真实经济或政治预测模型；
- 不加入 LLM、PCG、完整家庭/社交/战斗系统；
- 不生成 20,000 个同时可见的 UE Actor；
- 不让国王 AI 在主实验中自由选择政策；
- 不为了画面效果改变实验规则，也不把政策结果解释为现实政策结论。

---

## 2. 文献如何串起本项目

| 文献 | 本项目采用的启发 | 不应过度解读的边界 |
|---|---|---|
| Meadows《Thinking in Systems》[R1] | 用 Stock、Flow、反馈、延迟、边界和干预点描述王国；政策改变流量或信息连接，而不是写死结局 | 她没有给出本项目的森林、价格、进口或补助公式 |
| O’Halloran [R2] | AI 可以按层级降精度；离屏事件可预测/推进，玩家中途观察时按“已过去时间”恢复 | 论文原型不是 20,000 人的王国人口聚合模型 |
| Sunshine-Hill [R3] | 依据 criticality 在有限预算下分配 AI 特征；用行为不连续/不合理现象审查 LOD 切换 | LOD Trader 不负责生成王国经济和群体行为 |
| Navarro 等 [R4] | 大规模 Agent 可动态聚合；比较 CPU gain、事件/行为差异；聚合算子需明确 | 其城市模拟聚合规则不能直接复制为本项目 Cohort |
| Soyez 等 [R5] | 多层模拟需要 activation/deactivation、aggregation/disaggregation、memorisation；区分弱一致与强一致 | 本项目的字段、阈值和日志仍需自行操作化 |
| Nguyen 等 [R6] | 明确 System Dynamics 与 Agent-Based 两侧的接口方向、交换变量、频率和时机 | 不提供本项目具体王国参数 |
| Reynolds 等 [R7] | 多分辨率实体要保留跨表示的核心属性，并处理不同分辨率间的一致性 | `CoreState` 的具体 Schema 是本项目设计 |
| Costa 等 [R8] | 地震恢复会受到损伤、资金、劳动力和材料资源约束，并存在群体差异 | 本项目只有木材和两类收入，是故意简化的教学场景 |
| Barlas [R9] | 验证模型结构、行为模式和极端条件；不要把模型输出当作现实真值 | 本项目验证的是实现与相对行为，不是最优国家政策 |
| ODD / STRESS [R10][R11] | 把模型规则、随机性、实验场景、硬件和运行过程写清楚，保证复现 | 本文不是完整 ODD/STRESS 报告，正式论文需再整理 |

**研究边界：** 主实验固定国王政策，是为了让四种模拟方法接收完全相同的外部输入。否则一次运行中 A 方法可能遇到“限伐”，B 方法可能遇到“进口”，观察到的居民差异就无法归因于 LOD 表示。

---

## 3. 系统总览与权威状态

### 3.1 只有两种模拟语义，三种表示状态

- **宏观语义：** Structured Macro 或对照用的其他离屏 Backend 推进离屏人口。
- **微观语义：** Utility 选择目标，GOAP 选择达成目标的动作序列。
- **表示状态：** 一个居民同一时刻只能处于以下一种状态：

```text
Anonymous Cohort XOR Persistent Macro XOR Active Micro
```

`Persistent Macro` 不是第三套完整 AI。它只是一个保存 `PersistentID + CoreState + EventRef` 的轻量记录，不运行逐帧 GOAP。UE Actor 只是表现容器，销毁 Actor 不等于销毁居民。

### 3.2 数据流

```text
Fixed Scenario / Policy Timeline
              ↓
Authoritative Simulation Clock → Off-screen Backend → Cohort / Individual State
              ↓                         ↕
      Event Scheduler           Authoritative Ledger
                                         ↕
Criticality or Test Script → Macro–Micro Bridge → Utility + GOAP → UE Actor
                                         ↓
                                  Logs and Metrics
```

### 3.3 必须保持的全局不变量

1. 全系统只有一个权威游戏时间；宏观层只结算 `LastUpdateTime < t ≤ Now`，不得提前写入未来结果。
2. 人口守恒：

> `[INV-POP] N_total = N_anonymous + N_persistent_macro + N_active_micro`

3. 木材守恒：

> `[INV-WOOD] InitialWood + Growth + ExternalImport = Forest + Market + InTransit + ResidentInventory + EmbeddedOrRepaired + BoundaryConsumption`

4. 一个 `EventID` 同一时刻只能有一个 Owner；一次资源变化必须有唯一 `TransactionID / IdempotencyKey`。
5. LOD 切换保留 `EventID`、`StartTime`、`RemainingWork`、`ReservationID` 与因果政策，不能重新开始或重复收费。
6. Stock 不得为负。进口款、工作收入等跨系统边界流必须明确标为 `BoundaryFlow`。
7. 同一配置与 Seed 必须产生同一模拟结果；只有实际 CPU 时间可以不同。
8. 四种方法共享同一初始化、地震、政策、Utility、GOAP Action、持续时间和激活脚本。

### 3.4 建议模块边界

| 模块 | 唯一职责 | 不得承担的职责 |
|---|---|---|
| Simulation Clock / Scheduler | 统一时间、事件顺序和 catch-up | 不直接改变库存 |
| Canonical State Store | 保存 Kingdom Stock、Cohort、Persistent CoreState | 不执行 UI 或 Actor 生命周期 |
| Ledger / Reservation / Event Store | 资源事务、预留、事件所有权和幂等 | 不自行选择 NPC 目标 |
| Scenario Driver | 地震与固定政策时间表 | 不读取实验结果改变政策 |
| Off-screen Backend Interface | Structured、Simple 或 Per-Agent 推进 | 不复制一套不同的领域规则 |
| Individual Simulation | Utility + GOAP + 个体状态推进 | 不直接写 Kingdom Stock |
| LOD Selector / Transition Bridge | 名额、拆分、聚合、所有权转移 | 不发明居民历史 |
| Experiment Runner | 配置、Seed、方法、场景、激活脚本 | 不依赖镜头决定主实验样本 |
| Logging / Metrics | 只观察、导出和离线计算 | 不反向影响模拟 |
| UE Presentation Adapter | Actor、动画、Debug UI、固定地图 | 不成为权威状态 |

---

# 第一项：最小 Cohort 分类和变量

## 4. Cohort 是什么

Cohort 不是一个 NPC，也不是地图格子。它是“当前规则下可以被同样处理的一批匿名居民”。本文统一称最细一层为 **Cohort State Bucket（群体状态桶）**。

### 4.1 基础分类

> `[MVP-COHORT-01] BaseCohort = Kingdom × Profession × IncomeBand`

| 维度 | MVP 取值 | 为什么保留 |
|---|---|---|
| Kingdom | A、B | 政策、资源和地震边界不同 |
| Profession | Logger、Worker | Logger 可以合法砍木，行为路径不同 |
| IncomeBand | Low、NonLow | 影响收入、购买能力和 Repair Aid 资格 |

每个王国的初始比例 `[FROZEN][MVP]`：

| Profession | IncomeBand | 比例 | N=10,000 时人数 |
|---|---:|---:|---:|
| Logger | Low | 14% | 1,400 |
| Logger | NonLow | 6% | 600 |
| Worker | Low | 56% | 5,600 |
| Worker | NonLow | 24% | 2,400 |

### 4.2 动态状态桶

> `[MVP-COHORT-02] StateBucket = BaseCohort × HomeState × MacroIntent`

| 字段 | 取值 |
|---|---|
| HomeState | Healthy、DamagedWaiting、UnderRepair、Repaired |
| MacroIntent | Routine、Work、BuyWood、ChopWood、Repair、Wait |

两国理论上最多：

> `[MVP-COHORT-03] 2 × 2 × 2 × 4 × 6 = 192 个状态桶`

实际只存非空桶，因此用稀疏 Key-Value 容器。192 不是 192 个居民，而是最多 192 种“同类居民的当前处境”。未来增加职业、收入层或王国时仍可使用，但只有会改变政策资格、资源 Flow 或 Utility/GOAP 可行性的属性才应成为 Cohort 维度；其他属性保留为桶内统计或 Persistent CoreState，避免组合爆炸。[LIT] 这种“低关注时聚合、高关注时拆分”的方向与动态聚合、多层 ABM 一致 [R4][R5]，具体维度是本项目设定。

### 4.3 每个状态桶的最小变量

| 字段 | 类型/单位 | 用途 |
|---|---|---|
| PopulationCount | integer persons | 桶内匿名人数 |
| CashSum | coin | 总现金 |
| CashSquaredSum | coin² | 恢复个体时近似方差；也用于分布误差 |
| RepairCreditSum | coin | 只能用于维修的补助余额 |
| WoodCount[0..4] | integer persons | 每个持木数量的人数直方图；`4` 表示四块或以上 |
| AidEligibleCount | integer persons | 当前符合补助条件人数 |
| AidReceivedCount | integer persons | 防止重复领取 |
| RepairProgressBins[4] | integer persons | 维修进度 0–25%、25–50%、50–75%、75–100% |
| EventBatchRefs | list of IDs | 指向 Ledger 中批量事件，不在桶内复制事件真相 |
| LastUpdateTime | game timestamp | 桶结算到何时 |
| ResidualFlow[] | fixed-point residual | 小时步长下保存不足 1 人的期望流量，避免每步取整偏差 |
| RNGStreamKey | integer/hash | 稳定随机流，保证复现 |

`CashSum / CashSquaredSum` 是当前 MVP 的低成本近似，不保证恢复出 Oracle 的完整现金分布。若预实验发现现金分布误差成为主要来源，再升级为少量 Cash Histogram；正式实验开始后不得改变表示。

### 4.4 王国级 Stocks

| Stock | 单位 | 说明 |
|---|---|---|
| ForestCapacity | wood | 森林环境容量 K |
| ForestWood | wood | 当前可采木材 |
| MarketWoodAvailable | wood | 市场可售库存 |
| MarketWoodReserved | wood | 已被购买计划预留但未交付 |
| WoodInTransit | wood | 已购买、运输中进口木材 |
| WoodEmbeddedInRepairs | wood | 已投入未完成维修 |
| WoodInRepairedHomes | wood | 已进入完成房屋的累计木材 |
| TreasuryAvailable | coin | 可用国库 |
| TreasuryReserved | coin | 已下单未结算的国库款 |
| MarketCoin | coin | 居民购木支付进入市场的内部资金 |
| WoodPrice | coin/wood | 当前木材价格 |

### 4.5 数量推进与取整

宏观 Flow 可以是小数，但人数必须是整数。每条转换使用确定性余数累计：

> `[MVP-EQ-01] a_next = a_prev + rate × Δt; moved = floor(a_next); residual = a_next − moved`

这是一条本项目数值实现规则，不是文献公式。它避免“每小时四舍五入为 0”或长期系统性偏大，并保证同一 Seed 可复现。

---

# 第二项：三项政策的具体规则

## 5. 单位与初始值

令 `N` 表示单个王国居民数，`Δt = 1/24 day`。小规模实验每国 `N=100`；大规模人口平均分到 A、B 两国。以下全部是 `[MVP][PILOT]` 合成参数，不是文献报告的现实数值。

| 参数 | 初值/规则 |
|---|---:|
| ForestCapacity K | `20N wood` |
| Initial ForestWood F₀ | `16N wood` |
| Initial / Target MarketWood | `2N wood` |
| Initial Treasury | `5N coin` |
| Base WoodPrice P₀ | `1 coin/wood` |
| Baseline Harvest | `0.08N wood/day` |
| Baseline External Import | `0.02N wood/day` |
| Routine Boundary Consumption | `0.10N wood/day` |
| Earthquake Damage | A 国 30% homes |
| Repair Wood Requirement | `4 wood/home` |
| Repair Start Capacity | `0.01N homes/day` |
| Repair Duration | `2 game days` |
| Low initial cash | `[0,4) coin` |
| NonLow initial cash | `[4,8) coin` |
| Low / NonLow work income | `1 / 2 coin/day`，记录为外部收入 Flow |

### 5.1 森林增长

> `[MVP-EQ-02] Growth(F) = rF(1 − F/K), r = 0.025/day`

这是本项目采用的 Logistic 增长启发式。Meadows [R1] 支持用 Stock、Flow、承载限制和反馈思考系统，但没有给出本项目的方程或 `r`。每步实际增长为 `Growth(F) × Δt`。

### 5.2 市场价格

> `[MVP-EQ-03] P_target = clamp(P₀ × sqrt(TargetMarketWood / max(MarketWoodAvailable, ε)), 0.5P₀, 3P₀)`

> `[MVP-EQ-04] P_next = P + (P_target − P) × min(1, Δt / 1 day)`

这是库存不足形成负反馈、并带一天调整延迟的 `[MVP]` 价格启发式；概念受 Stock/Flow/feedback/delay [R1] 启发，公式不是 Meadows 原文。其目标只是产生可解释的资源压力，不是拟合真实市场。

## 6. Policy 1：Harvest Cap（限伐）

| 项目 | 规则 |
|---|---|
| 宣布 | Day 2 |
| 生效 | Day 3 |
| 结束 | Day 30 |
| Cap | `0.06N wood/day` |
| 适用 | 商业采伐 + 居民自砍合计 |

> `[MVP-EQ-05] ActualHarvest = min(DesiredHarvest, RemainingPolicyAllowance, ForestWood / Δt) × Δt`

先由 Ledger 预留当日剩余额度，再从 Forest 转出木材；不能由商业采伐和 `ChopWood` 各自重复使用 Cap。它测试一个规则变化如何沿“可采量 → 市场库存/个人取材 → 价格/维修 → 行为”传播。[LIT] 将规则、信息流或反馈连接当作干预点来自 Meadows [R1]；具体 Cap 和时表是 `[MVP]`。

## 7. Policy 2：State Import（国家进口）

为避免额外建立商人对补贴的反应模型，MVP 使用直接国家进口，不使用“进口补贴”。进口来源是系统外部，不从 B 国扣木；B 国是未受灾对照和跨国任务演示环境。

| 项目 | 规则 |
|---|---|
| 下单窗口 | Day 2–14 |
| 单日上限 | `0.08N wood/day` |
| 单价 | `1.25 coin/wood` |
| 总预算 | `1.0N coin` |
| 运输延迟 | 3 game days |

> `[MVP-EQ-06] DesiredImport = ExpectedRepairWoodUse + max(0, (TargetMarketWood − MarketWoodAvailable − WoodInTransit) / 3 days)`

> `[MVP-EQ-07] Order = min(DesiredImport, DailyCap, BudgetRemaining / ImportPrice)`

`ExpectedRepairWoodUse` 用未来 3 天维修开工容量与等待户数的较小值乘 4 估计。下单时 Treasury 从 Available 转为 Reserved，木材进入 `WoodInTransit`；三天后才进入 Market。取消或失败必须通过反向 Ledger 事务释放预留。该“目标库存缺口 + 管道库存 + 交付延迟”的结构是 `[ADAPTED]` 的库存调整逻辑；精确公式与参数为本项目设定，可用 Meadows 的延迟/反馈框架 [R1] 解释，不应声称来自某篇论文的原式。

## 8. Policy 3：Repair Aid（维修补助）

| 项目 | 规则 |
|---|---|
| 资格日 | Day 2 |
| 发放日 | Day 3 |
| 金额 | `2 coin/eligible home`，一次性 |
| 总预算 | `0.40N coin` |
| 资格 | DamagedWaiting + Low + 未领取 + `Cash + RepairCredit < 4P` |
| 用途 | 进入 RepairCredit，只能购买维修木材 |

> `[MVP-EQ-08] PaidCount = min(EligibleCount, floor(AidBudgetRemaining / 2))`

补助只改变购买能力，不生成木材，也不直接把房屋改为 Repaired。因此材料不足时，发钱可能先推高需求而非立即完成维修。这正是要观察的反馈链。贫富和恢复资源会影响地震后恢复过程的建模动机来自 Costa 等 [R8]；资格、金额和预算为 `[MVP]`。

## 9. 主实验政策控制

主实验分别运行：`None`、`HarvestCap`、`StateImport`、`RepairAid`。不组合政策，不让国王 Utility 自由挑选。组合政策和 King Policy Utility 只作为附加演示：国王可以有 `ForestHealth / RecoverySpeed / TreasurySafety` 三个政策目标，但不得混入主要比较，否则不同方法可能接收到不同输入。

---

# 第三项：地震场景的事件流程

## 10. 最小因果链

```text
地震 → 房屋受损增加
     → 维修木材需求增加
     → 市场库存下降 / 木价上升
     → 居民按能力选择购买、工作、自砍或等待
     → 森林下降、进口管道变化、维修完成率变化
     → 政策改变其中一条 Flow 或资格规则
```

这不是预写剧情。每一步只改变 Stock、Flow、可行动作或事件条件，后续结果由同一组规则推进。[LIT] 采用系统反馈和延迟的组织方法 [R1]；地震恢复中的材料、资金与异质性约束参考 [R8]。

## 11. Day 0 地震

1. 只影响 A 国；B 国保持未受灾对照。
2. 在每个 `BaseCohort` 内用同一固定 Seed 分层抽取精确 30%，保证职业/收入构成不因随机波动改变。

3. `Healthy → DamagedWaiting`，创建 `EarthquakeDamageEvent`。
4. 事件只记录当前事实，不预先提交哪天一定修好。
5. Oracle、Per-Agent、Simple 和 Proposed 使用相同受损身份清单或相同分层计数。

## 12. 居民最小决策域

Utility 只选择两个目标：

| Goal | Utility 规则 `[MVP]` |
|---|---|
| RestoreHome | HomeState 为 DamagedWaiting/UnderRepair 时 100，否则 0 |
| RoutineLife | 固定 10；住所恢复后成为最高可用目标 |

无需 LLM。GOAP 用显式前置条件和效果搜索“怎么做”。相同分数用固定 GoalID/ActionID 打破平局。

| Action | 关键前置条件 | 事务/效果 | 持续时间 |
|---|---|---|---:|
| Routine | 住所未阻止日常行为 | 记录 Routine 行为事件 | 8 h |
| Work | 仍缺购买资金 | `BoundaryIncome → ResidentCash`；Low +1，NonLow +2 | 1 day |
| BuyWood | Market 有货；Cash+Credit 足够 | 预留市场木材，扣款，最多补至 4 wood | 1 h |
| ChopWood | Profession=Logger；可达森林；有采伐额度 | `Forest → ResidentInventory` | 1 day |
| StartRepair | DamagedWaiting；持有 4 wood；有维修容量 | 4 wood 进入 Embedded，创建/接管 RepairEvent | 即时开始 |
| ContinueRepair | 已拥有 RepairEvent | 推进 RemainingWork，不重复扣木 | 共 2 days |
| Wait | 没有可行动作 | 记录等待原因 | 6 h |

宏观层用同一优先规则近似群体 Flow：

```text
已有 4 wood           → Repair
否则可负担且市场有货 → BuyWood → Repair
否则是 Logger         → ChopWood → Repair
否则                  → Work → BuyWood → Repair
都不可行              → Wait
```

## 13. 木材的权威路径

| 行为 | Ledger Source | Ledger Destination |
|---|---|---|
| 商业采伐 | ForestWood | MarketWoodAvailable |
| 居民自砍 | ForestWood | ResidentInventory |
| 市场购买 | MarketWoodAvailable/Reserved | ResidentInventory |
| 国家进口下单 | ExternalBoundary | WoodInTransit |
| 进口到货 | WoodInTransit | MarketWoodAvailable |
| 开始维修 | ResidentInventory | WoodEmbeddedInRepairs |
| 完成维修 | WoodEmbeddedInRepairs | WoodInRepairedHomes |
| 日常消耗 | 相关 Stock | BoundaryConsumption |

> `[MVP-EQ-09] RepairStarts = min(DamagedReady, RemainingRepairCapacity, floor(ResidentWoodReady / 4))`

开始维修时消耗/预留 4 wood，并创建 `EventID, StartTime, EndTime, ReservationID, OwnerType, ParticipantCount`。完成时只改变事件与 HomeState，不再次扣木。

## 14. 统一时间与中途观察

宏观步长是一小时，但系统不提前提交整个未来小时：

```text
12:00  LastUpdateTime = 12:00
12:17  玩家要求激活 NPC
       1) 将相关王国、桶和事件 catch-up 到 12:17
       2) 只执行 Timestamp ≤ 12:17 的事务
       3) 转移该居民的 Event / Reservation Owner
       4) 生成微观 Actor，继续剩余 1h43m，而不是重启 2 天维修
```

O’Halloran 的预测事件原型用下面的已过去比例对生命值等状态插值 [R2]：

> `[LIT-EQ-01] T = clamp((t − t₀) / D_predicted, 0, 1)`

本项目不直接预测整场王国未来，也不提前提交最终结果；它只推进到当前时间，并依据实际 `RemainingWork / EventDuration` 恢复已承诺事件。因此本节流程是受其“中途观察时恢复进度”启发的扩展，还额外要求 Cohort、资源 Ledger 和身份状态一起转移。

## 15. 固定 60 日时间线

| 时间 | 事件 |
|---|---|
| Day -7 → 0 | Warm-up，不计正式指标 |
| Day 0 | A 国地震 |
| Day 2 | 政策宣布；Repair Aid 计算资格；State Import 可下单 |
| Day 3 | Harvest Cap 生效；Repair Aid 发放 |
| Day 5 | 第一批进口到货 |
| Day 7 | 激活 10 个预登记 Persistent NPC，持续 1 日 |
| Day 14 | 分层激活 20 个匿名居民，持续 1 日后写回 |
| Day 30 | 再次激活 Day 7 的同一组 Persistent NPC |
| Day 45 | 同一时刻请求激活 20 个 Persistent NPC，测切换压力 |
| Day 60 | 结束并导出日志 |

---

# 第四项：宏观到微观的数据映射

## 16. 映射字段

| Macro 来源 | Micro / CoreState 字段 | 映射规则 |
|---|---|---|
| Kingdom | Citizenship、PolicyContext | 精确复制 |
| Profession | CanChopWood、IncomeRate | 查固定职业表 |
| IncomeBand | AidEligibility、IncomeRate | 精确复制类别 |
| HomeState | Home facts | 精确复制 |
| CashSum/SquaredSum | Money | 按桶均值/方差与稳定 RNG 采样，并同步扣减统计 |
| RepairCreditSum | RepairCredit | 按资格与余额约束采样 |
| WoodCount[0..4] | InventoryWood | 从直方图无放回抽取 |
| MacroIntent | ContextHint | 只作为上下文，不强迫继续未承诺计划 |
| EventBatchRef | CurrentEvent、Progress、Reservation | 拆分批事件并转移 Owner |
| Kingdom Stocks / Price | GOAP World Facts | 只读快照；写入走 Ledger |
| PolicyState | Policy Facts | 精确复制当前已生效规则 |

**关键区别：** 未产生资源承诺的 `MacroIntent` 可以在进入微观后由 Utility/GOAP 重新评估；已经有 `EventID / ReservationID` 的 Repair、Purchase、Import 等必须继续同一事件。

## 17. Anonymous → Active Micro：原子步骤

1. 将相关状态推进到当前权威时间。
2. 根据固定测试脚本或 criticality 选择完整状态桶，而不是只按职业随机挑人。
3. 锁定一个 `PopulationCount` 名额。
4. 以稳定随机流抽取 Cash、Wood 和 RepairProgress；不得放回。
5. 同步减少桶计数、总额、二阶矩和直方图。
6. 若属于批事件，从 `ParticipantCount` 拆出 1，并生成子 `EventID` 或 ParticipantRef。
7. 若目标需要长期身份，分配/读取 `PersistentID` 与 `HomeID`。
8. 通过一次 Ownership Transfer Transaction 转移事件和资源预留。
9. 生成或复用 UE Actor，并把 CoreState 映射到组件。
10. Utility 重新选 Goal，GOAP 规划未承诺的下一步。

任何一步失败都不得留下“人口已扣但 Actor 未生成”的半提交状态。实现可以先在数据层构造 TransitionRecord，全部验证后再一次提交。

## 18. Active Micro → Macro：写回步骤

1. 将权威时间同步到写回时刻，并停止该 NPC 开启新决策。
2. 通过 Ledger 提交截至该时刻已完成的动作；未完成动作只保存进度。
3. 根据最新 Profession、IncomeBand、HomeState、Intent 计算目标桶 Key。
4. 普通匿名 NPC 把人数、Cash、Wood 与进度统计加回目标桶。
5. Persistent NPC 不回匿名人数池，而是保存 `CoreState`，进入 Persistent Macro。
6. 未完成事件转给 Macro Event Store，保留同一 `EventID / ReservationID`。
7. Actor 与感知/动画组件可销毁；CoreState 和 Ledger 不销毁。

## 19. Persistent CoreState 最小字段

| 类别 | 字段 |
|---|---|
| 身份 | PersistentID、HomeID、Kingdom、Profession、IncomeBand |
| 个体状态 | Money、RepairCredit、InventoryWood、HomeState |
| 行为上下文 | CurrentGoal、LastCompletedAction、MacroIntent |
| 事件 | EventID、ParentEventID、StartTime、RemainingWork、ReservationID、CausalPolicyID |
| 粗位置 | LocationAnchor（Home/Market/Forest/Work/Travel） |
| 时间与复现 | LastUpdateTime、RNGStreamKey、Version |

位置只保存固定地图上的语义 Anchor；MVP 不把 Region 加入 Cohort 维度。匿名 Actor 激活时从其 Home/Work Anchor 的固定 SpawnSlot 生成。若后续研究空间传播，再把 Region 作为新维度，而不是现在引入 PCG。

## 20. Persistent 测试身份

小规模实验开始前，从公共 Initial Population Manifest 预登记同一组 Persistent IDs，并在 Oracle、Per-Agent 与 Proposed 中使用相同身份和初始状态。Simple Aggregate 只保留测试 ID 标签，不保留完整个体状态；重新激活时按总量重建，因此其身份/状态损失会被连续性指标记录。随机匿名样本只做分布比较，不做逐 ID Oracle 配对。

---

# 第五项：小规模与大规模实验参数

## 21. 四种方法的精确定义

| 方法 | 离屏表示 | 用途 | 明确限制 |
|---|---|---|---|
| Detailed Individual Oracle | 小规模全部个体按完整 Utility+GOAP 与事件规则推进 | 准确性参考 | 只跑总人口 200；不是现实部署基线 |
| Per-Agent Update LOD | 所有居民保留独立 ID/状态；离屏每游戏小时批量更新一次个体决策 | 现实性能基线 | Actor 仍最多 50；不能偷偷聚合 Cohort |
| Simple Aggregate | 每国只保留总人口、四种 HomeState 总量、总 Cash/ResidentWood、固定职业/收入比例和少量平均延迟队列 | 聚合下限基线 | 无状态条件分布、无个体事件进度、无完整 Persistent CoreState |
| Proposed Structured Macro | 稀疏 Cohort State Bucket + 分布统计 + Event Batch + Persistent CoreState + Ledger | 被研究方法 | 离屏不运行逐个 GOAP |

### 21.1 Simple Aggregate 冻结规则

Simple Aggregate 必须足以接收同一地震和政策，但不能逐渐长成 Structured Macro：

- 每国保存 `N, Healthy, DamagedWaiting, UnderRepair, Repaired, TotalCash, TotalResidentWood` 和相同 Kingdom Stocks；
- 始终使用初始 `LoggerShare=0.20`、`LowShare=0.70`，不记录这些比例在不同 HomeState/Intent 中的分布；
- `MeanCash = TotalCash/N`，购买能力按均值阈值判断；自砍人数按固定 LoggerShare 估计；Aid 资格按 `DamagedWaiting × LowShare` 估计；
- 进口和维修只保留按到期小时索引的 aggregate delay queue，不含个体/批次 EventID、Participant 或进度分布；
- 激活匿名 NPC 时按全王国平均值重建，写回只更新总量；
- 与其他方法共用 Clock、Ledger API、政策公式、维修耗木和事件持续时间。

这组方法的意义是回答：“仅保存平均数是否已经足够？”如果它和 Proposed 一样准确，Structured Cohort 的复杂度就没有被实验支持；如果它性能快但政策/连续性误差大，就能说明 Proposed 在成本与信息保留之间的价值。

## 22. 小规模准确性实验

| 项目 | `[FROZEN][TEST]` 设置 |
|---|---|
| 总人口 | 200；A/B 各 100 |
| Active Micro Cap | 全局 50 |
| 方法 | Oracle、Proposed、Simple、Per-Agent |
| 场景 | None、HarvestCap、StateImport、RepairAid |
| Pilot | 5 Seeds；不进入正式统计 |
| Formal | 30 paired Seeds |
| 时长 | Warm-up 7 日 + 正式 60 日 |
| 离屏步长 | Proposed/Simple/Per-Agent 均 1 game hour |
| 日志 | Kingdom 每小时；Cohort 每 6 小时；事件逐条 |
| 总正式运行 | `4 methods × 4 scenarios × 30 seeds = 480` |

公平控制：同一 Seed 共享 Initial Population Manifest、受损 ID、政策时间表、激活脚本、GOAP Domain、RNG 子流规则和输出采样时刻。运行顺序随机化。Oracle 中所有 200 个居民是逻辑详细模拟，不要求同时存在 200 个可见 Actor。

## 23. 大规模性能实验

| 项目 | `[FROZEN][TEST]` 设置 |
|---|---|
| 方法 | Proposed、Simple、Per-Agent；不跑 Oracle |
| 总人口 | 2k、10k、20k；A/B 均分 |
| Active Micro Cap | 全局 50 |
| 重复 | 每配置 10 次 |
| 真实时间 | 60 s warm-up + 300 s measurement |
| 总运行 | `3 × 3 × 10 = 90` |
| 场景 | 固定主压力场景；建议 Earthquake + StateImport，正式前冻结 |
| 环境 | 固定地图、镜头、分辨率、构建配置；VSync off；配置顺序随机化 |

60 游戏日压缩到 300 秒：

| 真实秒 | 游戏事件 |
|---:|---|
| 0 | Day 0 地震 |
| 10 | Day 2 政策 |
| 25 | Day 5 第一批进口 |
| 35 | Day 7 Persistent 激活 |
| 70 | Day 14 Anonymous 激活 |
| 150 | Day 30 同一 Persistent 再激活 |
| 225 | Day 45 批量激活 |
| 300 | Day 60 结束 |

性能运行禁用完整逐 NPC 调试日志，只保留每秒性能、关键事件计数和硬不变量；准确性日志与性能日志分开，防止“日志系统性能”掩盖 AI 方法差异。

## 24. 主实验与演示的选择策略分离

- **主实验：** 只按固定 ID/分层样本/时间表激活，保证方法接收相同工作负载。
- **演示或附加实验：** 使用 Sunshine-Hill 式 criticality [R3] 在 50 个名额内分配精度。距离只是一个输入，任务相关、准星观察、历史身份和事件承诺也可以提高分数。

> `[ADAPTED][MVP-EQ-10] Criticality = w_obs·Observed + w_task·PlayerTask + w_event·CommittedEvent + w_history·Persistent + w_distance·DistanceScore`

Sunshine-Hill 用 audacity `A` 与情境 criticality `C` 近似低精度行为被玩家察觉的风险 [R3]：

> `[LIT-EQ-02, conceptual] P(BIR) ≈ A · C`

本项目不直接把它当作可校准概率模型。上面的线性 Criticality 形式和权重不是 LOD Trader 原式；它只是把“按重要性而非纯距离分配预算”的原则实现成可调 MVP。`[PILOT]` 权重在附加演示前冻结，不进入主实验方法比较。

---

# 第六项：指标计算和日志格式

## 25. 宏观轨迹误差

对每个变量 `x`：

> `[TEST-MET-01] E_traj(x,M) = (1/T) Σ_t |x_M(t) − x_Oracle(t)| / S_x`

固定归一化尺度：人数/房屋计数 `S_x=N`；Forest `16N`；Market `2N`；Price `P₀`；Treasury `5N`；比例 `1`。准确性误差指标的使用方向与 O’Halloran 对近似结果误差的测量 [R2]、Navarro 对行为差异与计算收益的并列评估 [R4] 一致；本式与归一化尺度是本项目的 `[TEST]` 操作化，不声称是其原式。

建议主变量：DamagedWaiting、UnderRepair、Repaired、ForestWood、MarketWoodAvailable、WoodPrice、TreasuryAvailable。

## 26. 政策传播指标

先在同一方法、同一 Seed 中消掉无政策轨迹：

> `[TEST-MET-02] Δx_(M,p)(t) = x_(M,p)(t) − x_(M,None)(t)`

再与 Oracle 的政策效应比较：

> `[TEST-MET-03] E_policy(x,M,p) = (1/T) Σ_t |Δx_(M,p)(t) − Δx_(Oracle,p)(t)| / S_x`

同时记录：

- 方向是否相同；
- 首次超过预注册阈值的 Onset Time；
- Peak Magnitude 与 Peak Time；
- 回到基线 ±10% 的 Recovery Time；
- “政策 → Flow → Stock → 行为事件”的中间节点是否出现。

该差分设计是本项目的因果操作化，只用于同一合成模型内比较“方法有没有传播同一输入”，不证明现实政策有效。[LIT] 结构和行为模式验证的边界参考 Barlas [R9]。

## 27. 行为分布

按固定窗口统计 `Routine, Work, BuyWood, ChopWood, RepairStart, RepairComplete, Wait, AidReceived`。

> `[TEST-MET-04] TVD(P,Q) = 0.5 Σ_i |P_i − Q_i|`

`P` 为被测方法的行为占比，`Q` 为 Oracle。TVD=0 表示分布相同，1 表示完全不重叠。可补充 Jensen–Shannon Divergence，但不得看完结果后再决定哪个是主要指标。

## 28. 跨 LOD 连续性

### 28.1 Persistent 强一致

对预登记的同一 `PersistentID`，在 Day 7、Day 30 和 Day 45 前后检查：

- PersistentID、HomeID、Kingdom、Profession；
- Money、RepairCredit、InventoryWood、HomeState；
- EventID、任务进度、ReservationID；
- CurrentGoal 与激活后的 FirstAction。

字段级误差与不匹配率分别报告。`weak consistency / strong consistency` 的概念源自 Davis 与 Hillestad 的跨分辨率模型族 [R12]，本项目的验证流程同时参考 Soyez 等人的聚合、拆分与 memorisation 方法 [R5]；本字段集合是 `[MVP]`。

### 28.2 硬错误计数

以下正式实验目标必须为 0：

| 指标 | 含义 |
|---|---|
| IdentityMismatch | 同一 PersistentID 的不可变身份改变 |
| TaskReset | 已承诺事件在 LOD 切换时重新开始 |
| DuplicateCompletion | 同一事件完成两次 |
| EventOwnerConflict | 同一 EventID 同时有多个 Owner |
| DuplicateTransaction | 幂等 Key 重复提交 |
| NegativeStock | 任一 Stock < 0 |
| PopulationResidual | 人口守恒残差非 0 |
| WoodResidual | 扣除明确边界 Flow 后木材账本残差非 0 |

### 28.3 Potential BIR

自动日志标记：立即出现不可能状态、LOD 切换造成根本性断裂、长期行为明显不合理。分类思路来自 Sunshine-Hill 的 Behavioural Impulse Response [R3]。自动检测只能叫 **Potential BIR**；真正“玩家是否察觉/觉得突兀”需要玩家观察或用户研究，不能由日志替代。

## 29. 性能指标

- AI CPU：Mean、P95、P99、Max；
- Macro CPU、Micro CPU、Transition CPU 分项；
- Working Set 与 Peak Memory；
- 单次/批量 Activation Latency；
- 每帧 Budget Violation 次数与最长等待；
- 随人口规模的斜率；
- 相对 Per-Agent 的 speedup：

> `[TEST-MET-05] Speedup = CPU_PerAgent / CPU_Method`

Navarro 等把计算收益与行为差异同时作为多层聚合评估维度 [R4]；本项目具体采样和阈值为 `[TEST]`。

## 30. 预注册统计与暂定门槛

- 30 个正式 Seed 做配对比较；报告均值/中位数、95% paired bootstrap CI 和每个 Seed 散点。
- 性能 10 次重复报告 P50/P95、95% CI 与硬件/构建信息。
- 正式运行前冻结主要变量、主要指标和比较方向；不根据正式结果换指标。
- `[PILOT]` 暂定门槛：硬错误=0；关键政策/事件误差 `<0.10` 且比 Simple 低至少 25%；20k 时 Proposed P95 CPU 低于 Per-Agent；AI P95 `≤2 ms`、切换 P99 `≤5 ms`。后二者只有记录目标硬件与 Development/Shipping 配置后才有意义。

## 31. 日志文件与最小 Schema

所有文件必须含 `schema_version, experiment_id, run_id, method, scenario, seed, game_time`；Manifest 还要保存配置文件 Hash、Git commit、UE 版本、构建类型、硬件与日志模式。

| 文件 | 粒度 | 关键字段 |
|---|---|---|
| run_manifest.json | 每 Run | 全部参数、Hash、环境、开始/结束、有效性 |
| kingdom_timeseries.csv | 每王国每小时 | 全部 Stock、Price、HomeState totals、政策状态 |
| cohort_timeseries.csv | 每非空桶每 6h | Cohort Key、Count、Cash moments、Wood histogram、Intent |
| npc_snapshots.csv | 固定激活/快照点 | PersistentID、CoreState、Goal、FirstAction、EventRefs |
| simulation_events.jsonl | 每事件 | EventID、Type、Owner、Start/End、Participants、Cause |
| lod_transitions.jsonl | 每次转换 | From/To、Requested/Committed time、latency、bucket、result |
| ledger_transactions.jsonl | 每事务 | TransactionID、IdempotencyKey、Resource、Source、Destination、Qty、BoundaryFlag |
| performance_1s.csv | 每真实秒 | AI/Macro/Micro/Transition CPU、内存、active count、queue |
| metrics_summary.csv | 每 Run/跨 Run | 各指标、尺度、Oracle pair、CI 计算输入 |

示例 `ledger_transactions.jsonl`：

```json
{"schema_version":"1.0","run_id":"P-SI-0042","game_time":"D05T00:00",
 "transaction_id":"TX-000812","idempotency_key":"IMPORT-17-ARRIVE",
 "resource":"Wood","source":"WoodInTransit","destination":"MarketWoodAvailable",
 "quantity":80,"boundary_flag":false,"event_id":"IMPORT-17","policy_id":"StateImport"}
```

日志时间戳和随机流分配需符合 ODD/STRESS 强调的模型与运行透明度 [R10][R11]。日志只观察，不允许触发或更改模拟。

---

## 32. 数据所有权与方法公平性

| 数据/规则 | 权威 Owner | 四方法是否共享 |
|---|---|---|
| Game Time / Event order | Clock/Scheduler | 是 |
| Policy Timeline | Scenario Driver | 是 |
| Wood/Coin Transactions | Ledger | 是 |
| Utility Goals / GOAP Actions | Individual Domain Definition | 是 |
| Anonymous off-screen representation | 当前 Backend | 否，正是自变量 |
| Persistent CoreState | Proposed/Per-Agent/Oracle 精确；Simple 只标签 | 否，作为连续性能力差异 |
| Active UE Actor | Presentation Adapter | 是，最多 50 |
| Activation Script | Experiment Runner | 是 |
| Metrics | Offline Evaluator | 是 |

不允许 Structured、Simple 或 Per-Agent 各自实现不同版本的地震、维修耗木、政策或 Action。Oracle 与 Per-Agent 复用同一套个体行为，区别只能是离屏更新调度和规模。

---

## 33. 实现阶段与 Definition of Done

### 阶段 0：冻结数据契约

- 定义单位、枚举、配置、ID、随机子流、Manifest 与日志 Schema。
- **DoD：** 同一 Seed 两次生成完全相同的 Initial Population Manifest 与 Earthquake Damage List。

### 阶段 1：纯数据模拟核心

- 实现 Clock、Scheduler、Ledger、Reservation、Event Store 和守恒检查，不依赖 Actor。
- **DoD：** 空场景可运行 60 日；人口/木材残差和重复事务均为 0。

### 阶段 2：王国与 Structured Macro

- 实现 Cohort、Stock/Flow、地震、三政策、批事件和固定时间线。
- **DoD：** 四政策场景各跑 60 日，无负库存；日志能解释每次 Stock 改变。

### 阶段 3：最小 Utility + GOAP 与 Oracle

- 只实现本文动作表；先在 200 人纯数据模式运行。
- **DoD：** 受损居民能根据条件产生 Work/Buy/Chop/Repair/Wait，所有资源通过 Ledger。

### 阶段 4：Macro–Micro Bridge 与 Persistent

- 实现匿名拆分、Persistent CoreState、事件中途转移和写回。
- **DoD：** 维修 50% 时切换两次，EventID/RemainingWork/木材只结算一次；Day 7/30 同一 ID 连续。

### 阶段 5：两个 Baseline

- 依精确定义实现 Simple Aggregate 与 Per-Agent。
- **DoD：** 三个可部署方法使用同一 Backend 接口、Scenario、Domain 和日志；Simple 未引入 Cohort 分布，Per-Agent 未聚合。

### 阶段 6：Experiment Runner 与离线指标

- 一键运行方法×场景×Seed；从原始日志重算指标。
- **DoD：** `run_manifest.json` 可复现任一 Run；删除 `metrics_summary.csv` 后可完全重建。

### 阶段 7：UE5.4 表现与 Criticality 演示

- 接 Actor、固定地图、镜头、Debug UI；Blueprint 只配置和显示。
- **DoD：** 开/关表现层不会改变同 Seed 的模拟日志；任意时刻 Active Micro ≤50。

### 阶段 8：Pilot 与正式冻结

- 运行 5 个 Pilot Seeds；只校准 `[PILOT]` 项并保存参数 Hash。
- **DoD：** 输出冻结配置、Pilot 排除清单、硬件/构建说明和正式实验脚本；之后不再调参。

---

## 34. 新 Codex 会话不得自行改变的内容

以下全部 `[FROZEN]`：

- 一个地震、一种核心资源木材；
- 两个王国、两个职业、两个收入层；
- B 为未受灾对照；进口来自系统外部；
- 三项政策与四个单独政策场景；
- 四种模拟方法及第 21 节边界；
- 全局 Active Micro Cap=50；
- 小规模 200 人、30 正式 Seeds、60 游戏日；
- 大规模 2k/10k/20k，每配置 10 次，不运行 Oracle；
- 离屏步长 1 游戏小时和第 15 节激活时间表；
- 主实验固定政策和 NPC 选择，不使用镜头/距离改变样本；
- 固定地图，不加入 PCG、LLM、完整社交或额外资源；
- C++ 为研究逻辑，Blueprint 为配置和表现；
- 正式数据开始后不调参、不改 Schema、不换主要指标。

仅 `[PILOT]` 项可在 5 个 Pilot Seeds 后一次性校准：政策数值、初始 Stock、Criticality 权重、CPU 目标阈值与性能验收门槛。必须记录旧值、新值、理由和最终配置 Hash。

## 35. 实现前仍需记录的项目环境

这些不会改变模拟含义，但新会话必须在 Phase 0 写入 Manifest/决策记录：

- `[CONFIRMED]` `.uproject` 当前 EngineAssociation 为 UE 5.4；实现前仍检查本地可用引擎。
- `[OPEN]` 目标 CPU/GPU/RAM、操作系统版本与电源模式。
- `[OPEN]` Development 与 Shipping哪个作为正式性能构建，另一个是否仅作补充对照。
- `[OPEN]` Accuracy Runner 是否通过 UE Headless / Commandlet / 无渲染模式运行；不得另写第二套模拟。
- `[OPEN]` 最终 UE 类名、目录与模块拆分；不得因此改变本文责任边界。

---

## 36. 验收清单

### 模型正确性

- [ ] 同 Seed 可复现初始化、地震、政策和行为日志。
- [ ] 人口、木材、事件 Owner、事务幂等的硬错误全部为 0。
- [ ] 宏观层从不提前提交未来事务。
- [ ] Import、Income、Consumption 都明确标为 Boundary Flow。

### LOD 连续性

- [ ] 维修中途从 Macro→Micro→Macro 不重复扣 4 wood。
- [ ] Persistent NPC 的 ID、Home、Profession 不随 Actor 回收改变。
- [ ] 未承诺 Intent 可重规划，已承诺 Event 必须续接。
- [ ] Day 45 同时请求 20 人激活时记录真实延迟且 Active≤50。

### 实验公平性

- [ ] 四方法共享 Scenario、Domain、Seed 与 Activation Trace。
- [ ] Oracle 只用于 200 人准确性参考。
- [ ] Simple 没有 Cohort/个体事件偷渡；Per-Agent 没有聚合偷渡。
- [ ] 准确性与性能日志模式分离。

### 论文可追踪性

- [ ] 每个公式保留本文 ID 和 `[LIT]/[ADAPTED]/[MVP]/[TEST]` 标签。
- [ ] `run_manifest.json` 保存 Spec Version、Config Hash、Git Commit。
- [ ] 论文描述模型限制，不把政策结果写成现实结论。

---

## 37. 公式与方法来源矩阵

| ID | 方法/公式 | 分类 | 文献关系 |
|---|---|---|---|
| MVP-COHORT-01~03 | Cohort 与 192 状态桶 | `[MVP]` | 聚合/拆分原则受 [R4][R5] 支持；维度与数量为项目设计 |
| INV-POP / INV-WOOD | 人口与木材守恒 | `[MVP][TEST]` | Stock/Flow/系统边界来自 [R1]；具体账本项为项目设计 |
| MVP-EQ-01 | 整数余数累计 | `[MVP]` | 数值实现规则，无直接文献公式 |
| MVP-EQ-02 | Logistic Forest Growth | `[MVP]` | [R1] 支持受限 Stock/feedback 思维；方程和 r 非其原文 |
| MVP-EQ-03~04 | 库存价格与延迟 | `[ADAPTED][MVP]` | 受 [R1] 的库存反馈/延迟启发；精确公式为项目启发式 |
| MVP-EQ-05 | Harvest Cap | `[MVP]` | [R1] 支持规则作为干预点；具体 cap 为项目参数 |
| MVP-EQ-06~07 | 目标库存进口 | `[ADAPTED][MVP]` | Stock adjustment/pipeline delay 结构；精确公式为项目设计 |
| MVP-EQ-08 | Repair Aid | `[MVP]` | [R8] 支持资金/群体差异影响恢复；金额和资格自定 |
| MVP-EQ-09 | RepairStarts | `[MVP]` | [R8] 支持材料/能力约束；min 公式自定 |
| MVP-EQ-10 | Criticality Score | `[ADAPTED][MVP]` | [R3] 支持按 criticality 分预算；线性式与权重自定 |
| LIT-EQ-01 | 离屏预测事件的已过去比例 | `[LIT]` | 直接用于解释 [R2] 的中途插值；本项目改为实际事件进度 |
| LIT-EQ-02 | `P(BIR) ≈ A·C` | `[LIT]` | [R3] 的概念近似；本项目不把它当作已校准概率 |
| TEST-MET-01 | 归一化轨迹 MAE | `[TEST]` | 与 [R2][R4] 的误差/差异评估方向一致；尺度自定 |
| TEST-MET-02~03 | 政策效应差分误差 | `[TEST]` | 项目操作化；[R9] 仅支持结构/行为模式验证边界 |
| TEST-MET-04 | TVD | `[TEST]` | 通用概率分布距离；非本项目发明 |
| 强/弱一致性 | Persistent 与群体一致性 | `[ADAPTED]` | 采用 [R5] 的多层 consistency / memorisation 思路 |
| Potential BIR | LOD 异常分类 | `[ADAPTED]` | 分类来自 [R3]；自动日志不能替代玩家感知 |
| TEST-MET-05 | CPU Speedup | `[TEST]` | [R4] 支持同时比较 CPU gain 与行为差异；比值实现自定 |

---

## 38. 参考文献

**[R1]** Meadows, D. H. (2008). *Thinking in Systems: A Primer* (D. Wright, Ed.). Chelsea Green Publishing. Systems-thinking resources: https://donellameadows.org/systems-thinking-resources/ ; Meadows, D. H. (1999), *Leverage Points: Places to Intervene in a System*: https://www.donellameadows.org/wp-content/userfiles/Leverage_Points.pdf

**[R2]** O’Halloran, P. (2016). *A Multi-tiered Level of Detail System for Game AI*. MSc Dissertation, Trinity College Dublin. https://publications.scss.tcd.ie/theses/diss/2016/TCD-SCSS-DISSERTATION-2016-055.pdf

**[R3]** Sunshine-Hill, B. (2014). Phenomenal AI Level-of-Detail Control with the LOD Trader. In S. Rabin (Ed.), *Game AI Pro*, Chapter 14. https://www.gameaipro.com/GameAIPro/GameAIPro_Chapter14_Phenomenal_AI_Level-of-Detail_Control_with_the_LOD_Trader.pdf

**[R4]** Navarro, L., Flacher, F., & Corruble, V. (2011). Dynamic Level of Detail for Large Scale Agent-Based Urban Simulations. *Proceedings of AAMAS 2011*, 701–708. https://aamas.csc.liv.ac.uk/Proceedings/aamas2011/papers/C5_B67.pdf

**[R5]** Soyez, J.-B., Morvan, G., Dupont, D., & Merzouki, R. (2013). A Methodology to Engineer and Validate Dynamic Multi-level Multi-agent Based Simulations. In *Multi-Agent-Based Simulation XIII*, LNCS 7838, 130–142. https://doi.org/10.1007/978-3-642-38859-0_10 ; open manuscript: https://arxiv.org/pdf/1311.5108

**[R6]** Nguyen, L. K. N., Howick, S., & Megiddo, I. (2024). A framework for conceptualising hybrid system dynamics and agent-based simulation models. *European Journal of Operational Research, 315*(3), 1153–1166. https://doi.org/10.1016/j.ejor.2024.01.027

**[R7]** Reynolds, P. F., Jr., Natrajan, A., & Srinivasan, S. (1997). Consistency Maintenance in Multiresolution Simulations. *ACM Transactions on Modeling and Computer Simulation, 7*(3), 368–392. https://doi.org/10.1145/259207.259235

**[R8]** Costa, R., Haukaas, T., & Chang, S. E. (2021). Agent-based model for post-earthquake housing recovery. *Earthquake Spectra, 37*(1), 46–72. https://doi.org/10.1177/8755293020944175

**[R9]** Barlas, Y. (1996). Formal aspects of model validity and validation in system dynamics. *System Dynamics Review, 12*(3), 183–210. https://doi.org/10.1002/(SICI)1099-1727(199623)12:3%3C183::AID-SDR103%3E3.0.CO;2-4

**[R10]** Grimm, V., et al. (2020). The ODD Protocol for Describing Agent-Based and Other Simulation Models: A Second Update to Improve Clarity, Replication, and Structural Realism. *JASSS, 23*(2), 7. https://doi.org/10.18564/jasss.4259

**[R11]** Monks, T., Currie, C. S. M., Onggo, B. S., Robinson, S., Kunc, M., & Taylor, S. J. E. (2019). Strengthening the reporting of empirical simulation studies: Introducing the STRESS guidelines. *Journal of Simulation, 13*(1), 55–67. https://doi.org/10.1080/17477778.2018.1442155

**[R12]** Davis, P. K., & Hillestad, R. J. (1993). Families of Models that Cross Levels of Resolution. *Proceedings of the 1993 Winter Simulation Conference*. https://doi.org/10.1109/WSC.1993.718351

---

## 39. 版本与决策记录

| 版本 | 日期 | 状态 | 主要决定 |
|---|---|---|---|
| v1.0 | 2026-08-05 | 原型冻结候选 | 六项 MVP、外部进口、B 国对照、Simple 规则、阶段 DoD 与引用边界 |
| v1.1 | 2026-08-10 | 性能规模修订 | 旧值：1k/5k/10k/20k、120 Runs；新值：2k/10k/20k、90 Runs；原因：减少重复实验，同时保留低、中、高三个性能观察点；影响：性能实验配置由 12 组减为 9 组，小规模准确性实验不变；批准人：项目作者 |

正式实现过程中，任何改变 `[FROZEN]` 项的决定必须新增一行，记录“旧值、新值、原因、对已有 Run 的影响、批准人”。
