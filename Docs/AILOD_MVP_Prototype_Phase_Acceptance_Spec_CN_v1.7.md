# Hierarchical AI / Simulation LOD：MVP v1.7 Cohort 批量提交与动态解聚规格

**版本：v1.7**<br>
**日期：2026-08-18**<br>
**验收文档可读性规则增补：2026-08-19**<br>
**状态：研究方向与 6G-B0 规则已由项目作者于 2026-08-18 确认；B0 已随独立本地提交封板；B1 已由项目作者于 2026-08-19 确认并以 `90020f2` 封板；B2A 已实现并通过检查，等待项目作者确认；B2B—B5 尚未实现**<br>
**基准文档：`AILOD_MVP_Prototype_Implementation_Spec_CN.md` v1.1**<br>
**前序修订：v1.2、v1.3、v1.4、v1.5、v1.6**<br>
**工程证据：`AILOD_MVP_Phase6G_A_Checkpoint_CN.md`、`AILOD_MVP_Phase6G_B1_Checkpoint_CN.md`、`AILOD_MVP_Phase6G_B2A_Checkpoint_CN.md`**<br>
**用途：用结构化 Cohort 权威状态、批量 Claim/Event 和按需 Lift/Restrict 替代 Proposed 的每小时全员候选与个人提交；历史文档保留不改。**

## 1. 文档优先级、实施生效点与不变项

实现依次读取 v1.1、v1.2、v1.3、v1.4、v1.5、v1.6 和本文件。本文件只覆盖第 2 节明确列出的冲突；未覆盖内容继续有效。

v1.7 的模型规则已经冻结，但当前代码在 6G-B2 第一条权威 Batch 路径通过检查点前仍执行 v1.6 Proposed。实施期间必须区分：

- **v1.6 Current Proposed：** 当前个人 CoreState、个人候选和独立提交实现；
- **v1.7 Shadow Proposed：** 6G-B1 只在旁路计算，不修改权威结果或旧 Digest；
- **v1.7 Batch Slice Prototype：** 6G-B2A/B2B 在隔离的新后端测试夹具中验证 Batch Event/Ledger 语义，不与 v1.6 个人状态形成混合权威；
- **v1.7 Authoritative Proposed：** 6G-B3 在全部离屏动作都具备 Batch 路径后，才把 Cohort Joint State 一次性切换为未激活人口的权威；6G-B4 接入完整 Lift/Restrict，到 6G-B5 总验收通过后才成为正式 Proposed。

以下冻结项不变：

- 人口档位、两个王国、政策、场景、公式、游戏小时时序、Seeds、Runs 和正式统计方法；
- Oracle 只用于总人口不超过 200 的准确性参考；Simple、Per-Agent 和 Oracle 的方法边界；
- 全局 Active Micro Cap=`50`，以及 v1.6 Day 7/14/30/45 的固定 Activation Trace；
- Work、BuyWood、ChopWood、Start/ContinueRepair、Routine 和 Wait 继续只读取共享 Individual Domain 的条件、持续时间和单位效果；
- Clock、Scenario/Policy、Ledger、Scheduler、Event/Reservation、Audit 和 Observer 仍由同一 Runtime 统一拥有；
- 正式政策继续使用固定输入；动态 King、LLM、PCG、完整社交、Food、跨国运输和玩家任务不进入本阶段；
- Validation、Accuracy、Performance 成本隔离继续有效，不把验证、完整审计、快照或落盘成本计入生产算法。

允许一次性初始化读取 Phase 0 的全部 `N` 个身份并建立 Identity Registry 与初始 Cohort；这部分必须计入 Initialize 成本。初始化后，v1.7 Proposed 的每小时后台推进不得扫描全部 Identity，也不得按人口生成个人候选、个人事件或个人账本事务。

当 v1.7 在 B3 第一次成为完整离屏权威路径时：

- `SpecVersion` 更新为 `1.7`；
- 领域日志语义变化使 `SchemaVersion` 更新为 `1.2`；
- 重建 ConfigHash，并冻结新的 200/2k/10k/20k Digest；
- v1.6 Digest 只作为旧 Proposed 回归与 B1 Shadow 对照，不得要求 v1.7 逐字节相同。

## 2. v1.7 批准覆盖的旧边界

| 旧条目 | v1.7 批准替代规则 | 原因与影响 |
|---|---|---|
| v1.4 §3.2：每名 Proposed 居民永久保存准确动态 CoreState | 全部居民永久保存静态 Identity；只有 Active 拥有完整当前个体状态，玩家已观察居民可拥有稀疏 Continuity Capsule；未激活人口的当前动态事实由 Cohort Joint State 权威表示 | 移除每小时个人状态写回；静态身份内存仍为 `O(N)` |
| v1.4 §3.3—3.4：Cohort 只是从个人事实重算的缓存 | Cohort Joint State 是未激活人口的权威动态表示；Identity Registry 不保存当前 Money/Wood/HomeState/Event | 允许真正聚合人口推进，而非只聚合决策 |
| v1.6 §3.1：七维 Key 直接分组全部居民 | 外层 Key 固定为 `Kingdom × Profession × IncomeBand`；动作资格相关动态事实进入 Cohort 内稀疏联合格子 | 避免按居民重建 Key，同时保留状态相关性 |
| v1.6 §3.2：形成 `PopulationCount` 个候选并逐人复核 | 每个非空联合格子形成整数 Action Flow 和 Batch Claim；不得展开为个人候选 | 直接处理 6G-A 的 Candidate/ActionCommit 瓶颈 |
| v1.6 §3.2：成功者创建独立 EventID/ReservationID/ArriveID | 匿名离屏参与者共享 Batch Event/Reservation/Order Key；只有 Lift 或显式 Capsule 承诺需要 ParticipantRef/Child Event | 事件和事务数量随批次数量增长，而不是随人口增长 |
| v1.4 §3.5：Macro/Micro 先展开到个人统一排序 | Macro Batch Claim 与 Active `Count=1` Claim 进入同一资源 Scope，按第 7 节确定性整数配额竞争 | 不展开居民，同时禁止观察状态获得保留容量或固定优先级 |
| v1.4 §4.1：全部动态个人字段零时间往返逐项相同 | 零时间 Lift/Restrict 必须保持 Identity、Capsule、事件谱系、人口与资源总量不变；离屏经过时间后的个人状态是可测的确定性重建 | 连续性硬错误与近似准确性继续分开 |
| v1.6 `SchemaVersion=1.1` 与个人事件日志语义 | 权威 v1.7 使用 Schema `1.2` 的 Batch/Participant 语义和新 Digest | 日志必须诚实描述新的权威状态 |

v1.7 不把 Proposed 退化为 Simple。Simple 仍只保存王国总量、均值和 aggregate delay queue；Proposed 额外保存状态联合分布、Action Flow、Batch Claim/Event 谱系、全员稳定 Identity、确定性 Lift/Restrict 和可选 Capsule。

## 3. 研究主张、复杂度边界与人口不变量

v1.7 Proposed 的核心主张冻结为：

> 不是把 100,000 个 NPC 做得更轻，而是只让 100,000 个稳定身份存在，由稀疏 Cohort Joint State 和 Batch Event 推进远处社会；只有进入玩家相关范围的居民才按需 Lift 为完整 NPC，并在离开时 Restrict 回聚合状态。

目标复杂度为：

> `O(N)` 一次性身份初始化与静态身份内存 + `O(J + B + A + M)` 每小时动态工作

其中：

- `J` 为非空 Joint Cell 数；
- `B` 为本小时 Batch Claim/Event 数；
- `A` 为 Active Micro 数，始终 `≤50`；
- `M` 为需要处理的稀疏 Capsule/ParticipantRef 数；
- 每小时不得出现 `O(N)` Identity 扫描、个人候选构造、个人提交或个人事件日志。

Capsule 是 Identity 的可选记忆覆盖层，不是第三套人口或第三套 AI。人口不变量为：

> `[INV-POP-v1.7] N_total = Σ CohortJointCell.Count + N_active`

拥有 Capsule 但未激活的居民仍包含在某个 Cohort Joint Cell 中，不能再额外计一人。任何 Lift/Restrict 失败必须原子回滚，不得改变该等式。

## 4. 每类数据的唯一权威来源

| 数据 | 唯一权威来源 | 约束 |
|---|---|---|
| ResidentID、PersistentID、Name/Appearance Seed、HomeID、初始 Kingdom、Profession、IncomeBand | Identity Registry | 初始化后不可变；不得每小时全量扫描 |
| 未激活人口的 Count、HomeState、MacroIntent/Commitment、购买力/木材 Band、AidEligibility | Cohort Joint State | 整数稀疏格子；不是从个人动态状态重算的缓存 |
| Cohort/Joint Cell 的 Coin、RepairCredit、Wood 精确总量 | Ledger 中以 JointCellID 为作用域的聚合账户 | Joint State 只读暴露，不保存第二份可写余额 |
| 匿名离屏动作、进度、参与人数、Reservation 和因果政策 | Batch Event/Reservation Store | 以 ParticipantCount 表示匿名成员；只为稀疏特殊成员保存 ParticipantRef |
| 玩家附近居民当前完整状态和个人账户 | Active State + Ledger 个人账户 | 只在 Active 窗口存在，人数 `≤50` |
| 玩家已知身份、上次观察、谱系引用和重建游标 | Continuity Capsule | 不是当前 Money/Wood/HomeState 的第二真相，不进入 Cohort 决策 |
| 时间与到期顺序 | Clock/Scheduler | Batch 与个人事件使用同一小时管线 |
| Actor、动画、感知和 UI | Presentation Adapter | 只读，不参与 Digest，不回写研究状态 |

所有资源改变仍只经过 Ledger；Cohort、Capsule、Active State 或日志副本不得直接增减资源。

## 5. Cohort Key 与稀疏联合状态

### 5.1 外层稳定 Key

未激活人口只按以下稳定身份维度进入外层 Cohort：

> `CohortKey = Kingdom × Profession × IncomeBand`

ResidentID、Name、Representation、精确资源和动态状态不得进入外层 Key。若未来确需增加 Region，必须通过新的版本化覆盖，不得在 Pilot 或正式结果之后临时加入。

### 5.2 内层 Joint Cell

每个 Cohort 内按以下联合维度保存非空动态格子：

> `JointCellKey = HomeState × MacroIntent/CommitmentState × PurchasingPowerBand × WoodBand × AidEligibility`

- `PurchasingPower = Cash + RepairCredit`；继续使用 v1.6 的 `0–3 / 4–7 / 8+ coin`；
- `WoodBand` 继续使用 v1.6 的 `0 / 1–3 / 4+ wood`；
- `MacroIntent/CommitmentState` 只能来自共享 Action/Event 的现有状态，不增加可调行为参数；
- `AidEligibility` 是当小时政策与住宅状态下的资格布尔值；
- 各维度必须作为联合格子保存，不能用互相独立的边际比例相乘来伪造行动资格；
- 只迭代 `Count>0` 的格子，空格子在小时边界删除；
- `Σ Cell.Count`、Ledger 聚合账户和 Pending Batch ParticipantCount 必须可对账。

每个格子至少暴露：`JointCellID、Count、CashTotal、RepairCreditTotal、WoodTotal、PendingBatchRefs、LastUpdateTime`。资源字段读取 Ledger 聚合账户；不得另存可写副本。

### 5.3 初始化与格子迁移

- Initialize 可以一次性读取 Phase 0 个人清单，建立 Identity Registry、初始 Joint Cell 和聚合 Ledger 账户；完成后删除 Proposed 的未激活个人动态 CoreState；
- 当动作开始、完成、政策资格或资源 Band 改变时，以整数 Action Flow 从 Source Cell 移到 Target Cell；
- 同一小时先结算 T 的到期 Batch Event，再执行政策/Flow/价格，然后规划和提交新 Flow，最后结算至 T+1；继续遵守 v1.6 小时时序；
- 一次迁移必须同时满足 `SourceCount -= k`、`TargetCount += k` 和相关 Ledger/Event 变化；任何一项失败则全部不写。

## 6. Cohort 决策与 Action Flow

每个非空可决策 Joint Cell 每小时最多调用一次共享 Domain：

1. 从 Joint Cell 的 Band、状态和 Ledger 总量构造不带身份的合成代表；
2. 共享 Domain 返回首行动和单位动作参数；
3. 生成 `ActionFlow(SourceCellID, Action, RequestedCount)`，默认 `RequestedCount=Cell.Count`；
4. 已有 Pending Commitment 的参与者不重复进入新决策；
5. Wait/Routine、Work 和稀缺资源动作分别进入 6G-B2A、B2B、B3 路径；
6. 一个 Flow 只允许包含单位效果、到期时间和资源需求相同的参与者；不同参数必须拆成不同 Batch Claim；
7. 竞争或前置条件拒绝后形成整数 Wait Flow，不创建个人 fallback 候选。

Action Flow 只保存人数和效果，不得生成 `RequestedCount` 个个人对象。Validation 可在 200 人小规模对照中额外计算 Oracle/旧 Proposed 结果，但额外工作必须计入 Validation，不进入生产成本。

## 7. Batch Claim 与 Macro/Micro 统一竞争

### 7.1 Claim 数据契约

每个同质 Claim 至少包含：

`BatchClaimID、GameTime、ResourceScope、Action、SourceCellID、RequestedCount、PerParticipantDemand、CausalPolicyID、StableOrderKey`。

Active Micro 的个人请求使用同一结构，固定 `RequestedCount=1`。`Representation` 不得进入容量、权重或优先级公式；不得为 Active 预留资源，也不得让 Macro 绕过 Active。

`BatchClaimID` 与 `StableOrderKey` 必须只由固定 Seed、GameTime、ResourceScope、Action/单位参数、Cohort/JointCell 稳定 ID 或 Active ResidentID、CausalPolicyID 派生；不得使用内存地址、容器序号、线程到达时间或 `Representation` 标签。相同输入重放必须产生相同 Claim ID、配额和余数顺序。

### 7.2 确定性整数配额

同一 `GameTime + ResourceScope` 的所有 Claim 一起分配：

1. 共享 Domain 为每个 Claim 给出正整数单位需求 `d_i=PerParticipantDemand`，并把 Scope 可用量统一成同一整数资源单位 `C`；Repair Capacity 等按人数计的 Scope 使用 `d_i=1`，Market Wood 等按资源量计的 Scope 使用真实单位需求；
2. Claim 的资源请求为 `R_i=RequestedCount_i × d_i`，总请求为 `R=ΣR_i`；若 `R≤C`，全部授予；
3. 若 `R>C`，先计算统一满足比例 `α=C/R`，每个 Claim 的参与人数配额为 `p_i=α × RequestedCount_i`，基础授予为 `floor(p_i)`；
4. 基础授予后，按 `p_i` 的小数余数从大到小尝试再授予一人；相同余数使用 `Hash(Seed, GameTime, ResourceScope, StableOrderKey)` 稳定打破平局，只有剩余资源不少于该 Claim 的 `d_i` 才能授予；
5. 公平余数轮后若仍有可用资源，按同一稳定顺序继续填充仍有未满足人数且单位需求可放入的 Claim，直到没有任何完整参与者可授予；不得拆出半个人或部分动作；
6. 每个 Claim 满足 `0≤GrantedCount≤RequestedCount`，`RejectedCount=RequestedCount-GrantedCount`，并满足 `Σ(GrantedCount_i×d_i)≤C`；未获批人数进入 Wait Flow；
7. 当同一 Scope 的所有 `d_i=1` 时，该算法退化为按请求人数比例的确定性整数配额。

该配额近似取代 v1.4/v1.6 的逐 ResidentID 排序，是 v1.7 的研究模型变化。它保证每个请求参与者权重相同、没有 Representation 专属优先级，但不承诺与逐人稳定排序选出完全相同的居民；差异进入准确性指标。

已经提交的 Batch Claim 不因同一小时稍后的 Lift/Restrict 重新竞争。若正式 Trace 在规划前 Lift，居民从 Cell Count 中原子移出并作为 `Count=1` Claim 参与；镜头或表现开关本身仍不得改变正式实验 Trace。

## 8. Batch Event、Reservation、Ledger 与原子提交

### 8.1 Batch Event 最小字段

每个 Batch Event 至少包含：

`BatchEventID、ParentBatchEventID、BatchClaimID、Action、SourceCellID、TargetCellID、ParticipantCount、StartTime、EndTime、RemainingWork/ProgressBucket、BatchReservationID、CausalPolicyID、InheritedOrderKey、Status`。

匿名参与者不拥有个人 EventID 或 ArriveID。只有被 Lift 的参与者或必须保持已观察承诺谱系的 Capsule 才创建稀疏 `ParticipantRef`；不得为所有 Participant 预生成引用。

### 8.2 两段原子边界

每个 Batch Flow 继续使用两段提交：

1. **Batch Preflight：** 验证 Source Count、Joint Cell 状态、聚合余额、王国资源、Capacity、单位效果、Event/Reservation 参数和 Scheduler 时间，不写任何数据；
2. **Batch Commit：** 使用已冻结的 GrantedCount 一次性迁移 Count、提交聚合 Ledger 事务、创建 Batch Event/Reservation 并加入 Scheduler。单线程 Runtime 在两段之间不得推进时间或处理其他 Claim。

必须满足：

- `RequestedCount = GrantedCount + RejectedCount`；
- 每个同质 Flow 最多产生一组 Batch Event/Reservation/Ledger 写入，不随 ParticipantCount 展开；
- 每笔事务具有 `IdempotencyKey = Hash(BatchClaimID, Action, EffectIndex)`；
- Work 的工资/国库 Flow 也必须经过 Ledger，因此在 B2B 独立验收；
- Repair 已扣的 Wood、BuyWood 已支付的 Coin/Credit、Import 已到达的资源不得在 Split/Merge 时再次结算；
- 任何拒绝或故障注入前后，Joint Cell、Ledger、Batch Event、Scheduler、Reservation 和 ParticipantRef 均不得留下残留。

### 8.3 Split/Merge 谱系

- Lift 一个正在 Batch Event 中的参与者时，父 Batch `ParticipantCount -= 1`，创建带 `ParentBatchEventID` 和 `InheritedOrderKey` 的 Child Event/ParticipantRef；它继承已经发生的资源承诺和剩余进度，不重新进入竞争；
- Restrict 一个未完成个人事件时，优先合并到参数、到期时间、Reservation 语义和 Parent 谱系相同的 Batch Event；否则创建 `ParticipantCount=1` 的 Batch Event；
- Split/Merge 前后参与人数、已扣资源、Reservation 总量和 Scheduler 到期数必须相同；
- Child Event 可以取得新的实现级 EventID，但不得取得新的竞争优先级或重新扣款。

## 9. Identity Registry 与 Continuity Capsule

### 9.1 Identity Registry

每个 Resident 永久保存最小静态记录：

`ResidentID、PersistentID、NameSeed/DeterministicName、AppearanceSeed、HomeID、InitialKingdom、Profession、IncomeBand、IdentityVersion`。

Identity Registry 允许 `O(N)` 内存和按 ResidentID 的直接查询，但禁止每小时全表扫描。Kingdom、Profession 或 IncomeBand 若未来允许动态改变，必须用新的规格定义身份迁移；v1.7 MVP 不新增此功能。

### 9.2 Continuity Capsule

Capsule 只为固定连续性样本、曾经 Active 或显式故事相关居民按需创建，至少保存：

`ResidentID、LastObservedTime、LastObservedState、LastKnownCohortKey/JointCellKey、KnownCompletedActions、CommittedEventLineage、BatchCursor、CapsuleVersion`。

- Capsule 记录“玩家最后知道什么”和重建条件，不是当前资源/住宅状态的第二权威来源；
- Capsule 不运行 GOAP，不进入每小时全员循环，不额外增加人口或资源；
- 一次正式 Run 内 Capsule 不自动驱逐；其数量、内存和处理成本必须单列；
- Capsule 关联的未完成个人承诺可以用稀疏 ParticipantRef 保持精确谱系；普通匿名历史仍只保留批量人数；
- 离屏经过时间后重新 Lift 的当前个人状态允许与 Oracle 不同，但必须从 Capsule、Joint State 和 Batch 谱系确定性恢复，而不是恢复为初始值或无条件平均值。

## 10. Dynamic Lift / Restrict

### 10.1 Lift 原子步骤

1. 验证 ResidentID、Active Cap 和当前时间；超出 Cap 时确定性拒绝或排队，不改变权威状态；
2. 由 Identity 定位外层 Cohort；有 Capsule 时以其最后状态与 Batch 谱系为条件，否则在该 Cohort 的非空 Joint Cell 中按 Count 做确定性加权选择；
3. 若条件 Cell 已空，按固定字段距离和稳定 Key 选择最近合法 Cell，并记录 `LiftReconstructionFallbackCount`；
4. 从选定 Cell 原子移出一人；根据 Band、Cell Ledger 总量和独立 `LiftState` 随机子流确定性提取整数 Cash/Credit/Wood；提取值必须让剩余人口仍满足该 Band 的最小/最大可行总量；
5. 把提取资源从 JointCell 聚合账户转入该 Resident 的 Active 个人账户；
6. 若参与 Pending Batch Event，按 §8.3 Split，继承进度、Reservation、政策原因和优先级；
7. 创建完整 Active State，更新 Capsule，最后才允许表现层创建 Actor；
8. 任一步失败则回滚 Cell Count、Ledger、Event、Reservation、Scheduler、Capsule 和 Active State。

高开区间如 `8+ coin` 的提取上限由当前 Cell Total 和其余成员最低需求决定，不创造额外可调分布参数。Cash 与 RepairCredit 分拆也必须在各自总量可行区间内确定性完成。

### 10.2 Restrict 原子步骤

1. 停止表现层写入并读取权威 Active State；
2. 结算当前小时已经完成的个人动作；未完成承诺按 §8.3 Merge；
3. 用当前 HomeState、Intent、PurchasingPower、Wood 和 AidEligibility 计算 Target Joint Cell；
4. 把个人账户全部资源通过 Ledger 转入 Target JointCell 聚合账户；
5. `TargetCell.Count += 1`，更新 Capsule 的最后观察与谱系游标；
6. 删除 Active 个人动态状态和账户，最后销毁 Actor；
7. 任一步失败则全部回滚。

### 10.3 连续性边界

同一时间戳的零时间 `Cohort → Active → Cohort` 必须满足：

- Identity 和已有 Capsule 字段不被重抽；
- 人口、Coin、Credit、Wood、HomeState 计数和 Pending ParticipantCount 总量不变；
- Event/Reservation 谱系和剩余进度不重置；
- 不产生 DuplicateTransaction、DuplicateCompletion、TaskReset、OwnerConflict 或 Split/Merge residual。

居民离屏经过一个或多个小时后的个人状态是受控近似结果，不要求等于 Oracle；但同一 Seed、同一 Trace 必须相同，并继续通过资源守恒和硬连续性门。

## 11. 确定性、Digest 与日志 Schema 1.2

### 11.1 随机子流和稳定顺序

至少分离以下随机/Hash 域，避免增加一次 Lift 改变后续社会结果：

- `CohortPlan`：合成代表与 Action Flow；
- `ClaimRemainder`：Batch Claim 余数和平局；
- `LiftCell`：Joint Cell 选择；
- `LiftState`：资源与状态提取；
- `SplitOrdinal`：Child Event/ParticipantRef ID。

所有 Cohort、Joint Cell、Claim、Event、ParticipantRef、Capsule 和 Active 集合在进入 Digest、日志或有顺序效果的循环前必须按稳定 ID 排序；容器迭代顺序不得成为领域结果。

### 11.2 新领域 Digest

v1.7 Digest 至少覆盖：

- Identity Registry 的稳定身份摘要；
- 每个非空 Joint Cell 的 Key、Count 和 Ledger 聚合余额；
- Action Flow、Batch Claim 的整数结果；
- Pending Batch Event/Reservation、ParticipantRef 和谱系；
- Active State 与个人账户；
- Continuity Capsule；
- Kingdom/Ledger/Scheduler 权威状态和硬错误计数。

CPU、真实时间、内存、文件路径、硬件信息和 Profile 计数仍不得进入领域 Digest。

### 11.3 日志语义

权威 v1.7 使用 `SchemaVersion=1.2`：

- `run_manifest.json` 记录 `proposed_model_version、authority_mode、joint_state_version、claim_allocation_version、capsule_version`；`authority_mode` 至少区分 `v1.6_current / v1.7_shadow / v1.7_slice / v1.7_authoritative`；
- `cohort_timeseries.csv` 记录外层 Cohort、JointCellKey、Count、资源总量和 Pending ParticipantCount；
- `simulation_events.jsonl` 支持 `owner_type=Batch/Individual`，并记录 `participant_count、batch_claim_id、parent_batch_event_id、source_cell_id、target_cell_id、inherited_order_key`；
- `ledger_transactions.jsonl` 记录聚合账户或个人账户，但一笔 Batch 事务不得按参与人数展开；
- `npc_snapshots.csv` 只记录固定 Trace 的 Active/Capsule 样本，不恢复全人口逐小时日志；
- `lod_transitions.jsonl` 记录 Lift/Restrict、选中 Cell、Fallback、Split/Merge 和原子结果；
- `performance_1s.csv` 保留既有生产成本字段；新增计数进入 Manifest 的版本化诊断对象，不能破坏旧解析器而不升版。

B1 Shadow 仍输出 v1.6 权威日志与 Digest；Shadow 对账只能进入明确标记的工程诊断，不得伪装成正式 v1.7 Run。

B1—B4 的 Manifest 必须标记 `valid_for_formal_experiment=false` 和当前 authority mode。只有 B5 对完整 v1.7 路径完成准确性、硬错误、回放和规模验收后，Runner 才允许把 v1.7 Run 标记为正式有效。

## 12. 方法公平性与实验口径

| 方法 | 离屏权威状态 | 离屏工作 |
|---|---|---|
| Oracle | 200 人完整个人状态 | 每人详细 Utility/GOAP |
| Per-Agent | 全人口独立状态和个人承诺 | 每游戏小时逐居民更新；不得借用 Cohort/Batch |
| Simple | 王国总量、均值、aggregate delay queue | 不得获得 Joint Distribution、Batch Claim 谱系或 Capsule 条件重建 |
| Proposed v1.7 | Identity + Cohort Joint State + Batch Event + Active/Capsule | 非空 Joint Cell 决策、Batch 提交和按需 Lift/Restrict |

- 四方法继续使用相同 Scenario、Policy、Domain、Seed 和正式 Activation Trace；
- v1.6 Current Proposed 保留为工程消融对照 `ProposedExactCommit`，用于证明性能变化来自批量提交；它不自动增加为正式第五方法，也不替换预注册四方法矩阵；
- 200 人准确性继续以 Oracle 为参考，使用既有轨迹误差、政策效应误差、Participant 加权 TVD、FirstAction 和连续性指标；
- Batch 与个人 Event 数不能直接比较，行为频率按 `ParticipantCount` 加权；
- 不通过削弱 Per-Agent、增加其日志、改变其更新频率或给 Proposed 特殊资源规则制造 Speedup。

## 13. 正确性、近似信号与性能诊断

### 13.1 必须为零的硬错误

除旧硬错误外，v1.7 增加：

- `JointCellPopulationResidualCount`；
- `JointCellResourceResidualCount`；
- `BatchRequestedGrantResidualCount`；
- `BatchCapacityOverflowCount`；
- `DuplicateBatchCommitCount`；
- `BatchSplitMergeResidualCount`；
- `LiftRestrictResidueCount`；
- `CapsuleIdentityMismatchCount`；
- `NegativeJointCellCount`；
- `ActiveCapViolationCount`。

这些项目在 Validation、Accuracy、Performance 的 Run 结束完整 Audit 中都必须为 0。

### 13.2 允许非零但必须报告的近似信号

- Cohort/Oracle 行动分布差异；
- Joint State/Oracle 轨迹误差；
- `LiftReconstructionFallbackCount`；
- 激活时状态重建误差；
- FirstAction 不匹配率；
- v1.7 与 v1.6 `ProposedExactCommit` 的 Cohort/资源/行动人数差异。

这些信号不能为了通过测试而强制清零，也不能在看到正式结果后改 Key、Band 或分配公式。

### 13.3 必须采集的可扩展性计数

- `IdentityScanCountPerHour`；
- `ResidentTouchesPerHour`；
- `NonEmptyCohortCount/JointCellCountPerHour`；
- `ActionFlowCountPerHour`；
- `BatchClaim/Event/LedgerTransactionCountPerHour`；
- `ParticipantCountPerHour`；
- `Capsule/ParticipantRefCount`；
- `Lift/Restrict Count` 与 P95/P99 Latency；
- Production/Macro/Micro CPU 和 Peak Memory。

`ParticipantCount` 随人口增长是合法的整数社会规模；Batch 对象数、ResidentTouches 和每小时动态工作若近似随人口增长则说明重构没有解决 6G-A 问题。

## 14. Phase 6G-B 增量步骤与检查点

### 验收文档必须让项目作者直接看懂

从 6G-B1 起，本阶段以及后续阶段的验收文档必须遵守以下写法：

- 验收文档的第一读者是项目作者，不能假设读者已经了解代码、论文术语或前序讨论；
- 开头先用大白话说明“这一步做了什么、为什么做、有没有改动真实模拟结果”；之后再给技术细节；
- 能用普通中文说明时，不单独使用专业名词。确实必须保留的代码名或研究名词，第一次出现时必须紧跟一句中文解释；
- 不得只写“事件谱系、原子失败、逐步门、权威切换”等词。应分别写成“记录事件的来龙去脉”“失败时全部撤销，不能只改一半”“完成后先停下，等作者确认再继续”“从这一步开始由新数据负责真实结果”等能直接理解的句子；
- 数字和测试结果必须同时说明它们能证明什么、不能证明什么，不能只堆字段名、计数器或英文缩写；
- 检查点结尾必须明确列出：已经完成、尚未完成、下一步是什么，以及是否已经提交或推送。

### 6G-B0 — v1.7 冻结设计

- 只新增本规格、权威规则索引、增量计划与 B0 检查点；
- 不修改 Source、测试、配置、现有 Digest 或日志；
- 明确旧规则覆盖、权威来源、精确/近似边界、Batch Claim、Capsule、Lift/Restrict、Schema/Digest，以及每一步完成后必须先停下等待作者确认；
- 文档一致性检查通过后停止，等待作者确认并独立提交。

### 6G-B1 — Shadow Cohort

- v1.6 Proposed 继续作为唯一权威结果；旁路构建 v1.7 Identity、Joint State、Action Flow 和 Batch 结果；
- 对账人口、Coin/Credit/Wood、HomeState/Joint State、行动参与人数和 Pending ParticipantCount；
- Shadow 不写权威 Ledger/Event/Scheduler，不改变旧 Digest；
- 200/2k/20k 固定工程 Run 通过后停止，等待确认。

### 6G-B2A — Wait / Routine Batch 切片

- 在隔离的 v1.7 Batch 测试夹具中只实现不涉及资源事务和稀缺容量的 Wait/Routine；
- 不按 ParticipantCount 创建事件或 fallback；
- 当前 v1.6 Proposed 继续是完整 Run 的唯一权威；不得把新 Joint State 与旧个人 Money/HomeState 组成混合真相；
- 回归硬错误、确定性、Action Count 和事件 Participant 对账后停止。

### 6G-B2B — Work / Ledger Batch 切片

- 在同一隔离夹具中增加 Work 的工资、国库和到期 Flow；
- 验证聚合 Ledger 事务、整数付款、幂等和资源守恒；
- 当前 v1.6 Proposed 仍不切换；未经确认不进入稀缺资源竞争。

### 6G-B3 — BuyWood / ChopWood / Repair / Reservation 批量化

- 实现 §7 的 Macro Batch 与 Active `Count=1` 统一 Claim；
- 实现 Batch Preflight/Commit、故障注入、Reservation 和事件谱系；
- Market、Forest、Repair Capacity 均通过混合竞争、原子失败和资源守恒测试；
- 只有全部共享离屏动作都有 Batch 路径后，才允许一次性从 v1.6 个人动态真相切换为 v1.7 Joint State + 聚合 Ledger + Batch Event 权威；不允许长期双写或按动作混用两套权威状态；
- B3 的权威 Macro 可在无动态 Trace 的测试会话中验收；完整正式 Activation Trace 留给 B4。

### 6G-B4 — Dynamic Lift / Restrict

- 实现 Identity 直接查询、Capsule、Joint Cell 提取/写回和 Batch Event Split/Merge；
- 覆盖固定 Trace、样本外动态居民、零时间往返、维修中途两次切换和 Active≤50；
- Activation/Restrict 成本不得进入 Macro Batch 成本。

### 6G-B5 — 新 Proposed 总验收

- 200 人对 Oracle 报告既有准确性、政策效应、TVD、FirstAction 与连续性；
- 2k/10k/20k 验证确定性、回放、日志重建、硬错误和生产成本；
- 50k/100k 只做工程压力测试，不运行 Oracle；
- `[ENGINEERING TARGET]` 同一工程环境 20k Proposed Production 至少比 Per-Agent 快 3 倍；未达到时停止并复核，不把目标冒充正式统计结论；
- 人口从 20k 增至 100k 时，Identity 数可增长 5 倍，但每小时 Identity Scan 必须为 0；Batch 对象数、ResidentTouches 和后台动态工作不得近似增长 5 倍；
- Active 始终 `≤50`，所有硬错误为 0；
- B5 通过前不开始 Pilot、正式 480/90 Runs 或 Phase 7 动态演示接入。

每个步骤必须使用同一 `phase-6g-b-cohort-batch` 分支上的独立本地提交，完成 Development Editor 编译、全部既有测试和本步新增测试，更新检查点后停止等待作者确认；未经明确要求不推送。B0 是纯文档例外，只要求文档/版本/Git 一致性检查，不重复运行未受影响的 UE 二进制测试。

## 15. v1.7 决策记录

| 版本 | 日期 | 决定 | 原因与影响 | 批准人 |
|---|---|---|---|---|
| v1.7 Representation | 2026-08-18 | 全员只永久保存静态 Identity；未激活动态状态由 Cohort Joint State 权威表示，Active 才有完整个人状态，Capsule 稀疏按需存在 | 把每小时动态成本从人口规模中解耦；Identity 初始化/内存仍为 `O(N)` | 项目作者 |
| v1.7 Batch Commit | 2026-08-18 | Action Flow、Batch Claim/Event 和聚合 Ledger 事务取代匿名居民个人候选与提交 | 直接处理 6G-A 20k 中 ActionCommit 占已归因时间约 91% 的瓶颈 | 项目作者 |
| v1.7 Competition | 2026-08-18 | Macro Batch 与 Active `Count=1` 使用同一确定性整数配额；Representation 不提供优先级 | 不展开居民，同时维持资源池公平性和可复现性 | 项目作者 |
| v1.7 Dynamic LOD | 2026-08-18 | Identity + Capsule + Joint State 按需 Lift，离开时原子 Restrict；Batch Event 可 Split/Merge | 保持身份和已知谱系，允许离屏个人当前状态成为可测近似 | 项目作者 |
| v1.7 Evidence | 2026-08-18 | v1.6 保留为工程消融；v1.7 使用新 Spec/Schema/Digest，并以 200 准确性和 2k—100k 可扩展性分别验收 | 避免把模型改变伪装成无损优化或提前声称正式 Speedup | 项目作者 |
| v1.7 Checkpoint Readability | 2026-08-19 | 验收文档必须先用大白话说明，再补充必要技术名称，并明确证据能证明和不能证明的内容 | 验收文档是给项目作者确认阶段成果使用，不能要求作者预先掌握代码或论文术语 | 项目作者 |
