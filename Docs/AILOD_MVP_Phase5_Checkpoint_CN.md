# AILOD MVP Phase 5.1 检查点

形成日期：2026-08-16<br>
最终复验与作者确认日期：2026-08-17<br>
分支：`phase-5-unified-backends`<br>
Phase 4 基线提交：`dd6e349cd69bc08c08cbac75b055f047b05f2e63`<br>
当前状态：Phase 5.1 实现与自动验收通过；项目作者已确认；本检查点随 Phase 5.1 封板提交，未推送

## 1. 为什么重开 Phase 5

首次 Phase 5 实现虽然通过 18 项自动化测试，但对抗复核发现两个构念效度问题：

1. Proposed 使用精确 Cash/Credit/Wood 等全部决策事实分组，组内计划必然与逐居民 GOAP 相同；固定 Seed 下 Proposed、Oracle、Per-Agent 的事务数和事件数完全一致。该实现更接近精确等价缓存，无法单独支撑有误差—性能权衡的 Simulation LOD 主张。
2. 固定 Trace 缺少原规格 Day 14/15 的 20 人分层激活；Simple 只改变 Active 人数标签，没有临时重建个人状态、运行个人 GOAP 或写回总量，因此激活工作量不公平。

项目作者批准路线 B：保留全员轻量 CoreState 和每居民独立承诺事件，但把 Proposed 改为受控 Cohort 近似；同时修复工程风险。正式覆盖规则见 `AILOD_MVP_Prototype_Phase_Acceptance_Spec_CN_v1.6.md`。

## 2. Phase 5.1 实现范围

### 2.1 受控 Cohort 近似

- Proposed 的决策 Key 改为 `Kingdom × Profession × IncomeBand × HomeState × MacroIntent × PurchasingPowerBand × WoodBand`。
- PurchasingPower Band 固定为 `0–3 / 4–7 / 8+`；Wood Band 固定为 `0 / 1–3 / 4+`。精确个人余额仍只存在于 CoreState/Ledger，不进入 Key。
- 每个 Cohort 用 Cash、RepairCredit、Wood 的整数均值构造无身份合成代表，每小时只运行一次共享 GOAP，再按稳定竞争 Key 分配到真实居民。
- 每个实际提交仍使用居民真实余额、职业、HomeState、王国资源和容量重新验证；失败执行 Wait，不借用 Cohort 均值或他人资源。
- 每名成功居民继续拥有独立 EventID、Owner、ReservationID、ArriveID 和进度。
- `CohortDecisionDisagreementCount` 与 `CohortAllocationFallbackCount` 是近似诊断，不再作为硬错误；TaskReset、资源残差、非法引用和重复提交仍必须为 0。

### 2.2 完整激活 Trace 与 Simple Micro

- 恢复 Day 14 激活、Day 15 写回：排除固定连续性样本，A/B 各 10 人；每国 `Logger Low / Logger NonLow / Worker Low / Worker NonLow = 1 / 1 / 6 / 2`。
- 四方法共享 Day 7/8、Day 14/15、Day 30/31、Day 45/46 的 ResidentID 和时间，共 60 次激活实例。
- Simple 激活时从可拆分 HomeState 总量和 Cash/Credit/Wood 均值建立临时 CoreState，把资源转入临时 Ledger 账户，并运行共享个人 Utility/GOAP。
- Simple 降级时把余额和 HomeState 写回总量；未完成个人事件保留时间、EventID、ArriveID 和 ReservationID，转换为无个人 CoreState 的 aggregate delay entry。
- 已经属于 Busy/UnderRepair aggregate event 的参与者不会被重复拆出；最终临时 CoreState 数为 0。

### 2.3 工程风险修正

- 新增共享 `FIndividualActionEvaluation`：GOAP 搜索与实际个人提交共同读取 Work/Buy/Chop/Repair/Routine/Wait 的前置条件、时长、付款、收入和状态结果；Simple aggregate 至少共享动作时长、收入和冻结 Domain 参数。
- BuyWood、ChopWood、StartRepair、StateImport 在任何写入前完成 Preflight；增加一次性故障注入与提交前后指纹比较。
- `RejectedActionResidueCount` 现在由实际故障测试驱动；四类故障各触发 1 次，残留均为 0。
- `TaskResetCount` 现在实际比较切换前后的 EventID、ArriveID、ReservationID、StartTime 和 EndTime。
- 每次激活记录 FirstAction；60 次激活全部有记录。
- 小时语义改为推进并结算到 T+1 后，再在同一状态上执行 Audit 和 T+1 Snapshot；激活/降级也在边界审计和快照前提交。
- 新增 Validation / Accuracy / Performance 三种运行模式。逐成员近似复算单列为 `ValidationPlanningEvaluationCount`；完整 Audit 与 Snapshot 的居民访问量单独记录。Performance 模式不执行逐成员复算，只在初始化和 Run 结束做完整 Audit。

## 3. 版本与输入 Hash

- `SpecVersion=1.6`
- `SchemaVersion=1.1`
- v1.5 ConfigHash：`8E7BF5AB0F52D85475BEBE10559B5BDECE010AA0`
- v1.6 ConfigHash：`37B74F7F3812E2EDC2C7915DA6E4612B7A488E11`

固定 200 人、Seed `20260810` 的 v1.6 输入 SHA-256：

| 输入文件 | SHA-256 |
|---|---|
| `initial_population_manifest.json` | `6D197DB07891BD867C996644A09C8AC5469A131FE7D99B03EC3A3D0B6E98DB82` |
| `earthquake_damage_list.json` | `D1D97F3CD4943D73D8F51CA14CCCA4A8FB04F01D294D852352A1CEA7DF5C1565` |
| `persistent_test_pool.json` | `1DCABBE46562FF997E8EC6C0B04A82D2BC18FD003EA6B25F36FF6649831E7C1B` |

ConfigHash 与三个输入 Hash 的变化来自序列化 `SpecVersion` 从 1.5 更新到 1.6；Manifest、Damage 和身份生成领域规则未改变。

## 4. 历史回归 Digest 迁移

Phase 2 的领域终值、事务数量和事件数量不变；Digest 只因 ConfigHash 更新：

| Phase 2 场景 | v1.6 Digest |
|---|---|
| None | `9ECC0E14CB0AC7FF6C1EBC75E17DA4D5EA27CB0C` |
| HarvestCap | `34770DC1D188FE4005BFBD4F12694606E465BF1A` |
| StateImport | `58D519E8F8897E2A1EBF62CCD498F830E2E040FE` |
| RepairAid | `B32FBCBCB554D2B1D11DF1B97495286A1D058240` |
| 20k StateImport | `5135DC003CBB46EA03E773277D921A0ECBD72589` |

Phase 3 历史 Oracle Runner 的四个 Digest 与 v1.4/v1.5 完全相同，证明共享动作解析重构没有改变冻结 Oracle 语义：

- None：`3FFFF68FF4CAAC749779F2707DB1CA778FBB27D9`
- HarvestCap：`3EFC44178FC73EBB8AF30A082E622514C9DA269A`
- StateImport：`0E9CA31A0C8ECF488949B41588A8C51B589B9C7C`
- RepairAid：`70D7E19D8B175A14C6AF759890CAD18D9DA22DD6`

Phase 4 固定确定性轨迹的 v1.6 Digest：`B8C8AE1CB8EEDFB3262199018D4E8054E007B00A`。变化来自 ConfigHash；Bridge 六项行为测试继续通过。

## 5. Phase 5.1 小规模回归基线

以下只用于固定 Seed 实现回归，不替代 30 paired Seeds 的正式准确性实验：

| 方法 | 场景 | Digest | Transactions | Events | 生产规划 |
|---|---|---|---:|---:|---:|
| Oracle | None | `83B3353C53E50E596BA3271AE90D499A2297E1EC` | 13066 | 40332 | 40331 |
| Oracle | HarvestCap | `DB7D90ED4C581C367ED1D91B0BC4C8EA82B80C62` | 12907 | 40326 | 40322 |
| Oracle | StateImport | `CC2592820898E215ADD9691B64B9825818638853` | 13106 | 40343 | 40331 |
| Oracle | RepairAid | `21A7BA1FF047EFC2A98C909E9BD99FA668AF8075` | 13086 | 40361 | 40358 |
| Proposed | None | `1D9D5CE8617007E90463C21F24639CDE939F16D7` | 13069 | 40323 | 2782 |
| Proposed | HarvestCap | `3DA2EF5439219E649F9EE5FA7C6723FCB2E06C9A` | 12907 | 40326 | 2782 |
| Proposed | StateImport | `D326B24A3D74128C955667DB42E8F1BADA9BC9CD` | 13109 | 40334 | 2782 |
| Proposed | RepairAid | `27DD9F42305630BD2417894164F8CDE6DF4725D4` | 13086 | 40361 | 2775 |
| Per-Agent | None | `9D5D1AF23E6EB0B119AF612DF564456E69F170C3` | 13066 | 40332 | 40331 |
| Per-Agent | HarvestCap | `68651039E3A2B88B7E719D4CA86035F3348B5566` | 12907 | 40326 | 40322 |
| Per-Agent | StateImport | `7CC66C5D5C848EDA313912740394096445DE008F` | 13106 | 40343 | 40331 |
| Per-Agent | RepairAid | `CA6FDA571E5FFB818BC78DA298ADFD70965CE76F` | 13086 | 40361 | 40358 |
| Simple | None | `1636E56E6E4B9C43A7F2275EB69B5012B7FDF760` | 13093 | 246 | 180 |
| Simple | HarvestCap | `C72A6297F310324806DC449535760009729F316C` | 12694 | 278 | 184 |
| Simple | StateImport | `5D87C9919198C408EF9C7063DACD2162FF12E31B` | 13186 | 258 | 180 |
| Simple | RepairAid | `385A79A3167F4BA01985A0D0CE3F782AA0E724F6` | 13094 | 247 | 180 |

关键解释：

- Proposed 与 Oracle/Per-Agent 已不再结构性完全等价。例如 None 为 `13069 / 40323` 对 Oracle 的 `13066 / 40332`，StateImport 为 `13109 / 40334` 对 `13106 / 40343`。
- HarvestCap 的 Validation 固定轨迹记录：生产规划 `2782`、额外验证规划 `40142`、Cohort 决策分歧 `7`、个人合法性或稀缺资源回退 `1480`。分歧非零证明近似路径真实存在；该数量本身不是正式准确性结论。
- 同一 Proposed/HarvestCap 在 Validation 与 Performance 模式产生相同确定性 Digest；完整 Audit 次数分别为 `1609` 和 `2`，Performance 的逐成员验证规划为 `0`。
- Simple 的生产规划不再为 0：四场景分别为 `180 / 184 / 180 / 180`，来自固定 Trace 的真实临时 Micro 规划。

## 6. 自动验收结果

- 2026-08-17 最终复验，UE 5.4 Development Editor：编译成功。
- 2026-08-17 最终复验，NullRHI 全套 `AILODResearch`：`18/18 Success`，`0 Failed`，自动化错误 `0`，最终退出码 `0`。
- Phase 0–4：`14/14 Success`；Phase 5：`4/4 Success`。
- 200 人 `4 methods × 4 scenarios` 全部完成 168 小时预热和 1440 小时正式阶段，精确停在 D60。
- 四方法每 Run 均记录 60 次激活与 60 个 FirstAction；Day 14/15 各处理 20 人，分层与排除连续性样本自动验证通过；Active Micro 最大值不超过 50。
- Simple 每 Run 重建 60 次、写回 60 次，最终临时 CoreState 为 0。
- BuyWood、ChopWood、StartRepair、StateImport 故障各注入 1 次；`RejectedActionResidueCount=0`，故障 Run 仍通过全部硬错误门。
- 人口、Wood、Coin、Event Owner、Reservation、DuplicateTransaction、DuplicateCompletion、TaskReset、FirstAction 缺失和到期未结事件均为 0。
- Simple、Per-Agent、Proposed 在 2k、10k、20k StateImport 的 67 游戏日 Performance-mode 冒烟全部通过。该结果只证明可运行，不是正式性能数据。

## 7. 当前结论与明确边界

Phase 5.1 已解决本轮对抗复核的两个阻塞项：

1. Proposed 已从精确等价缓存变成可测、确定性、保持个人承诺事实的受控 Cohort 近似；
2. Simple 现在承担相同固定激活 Trace 的真实临时 Micro 工作，并保持其离屏总量/均值能力下限。

仍然不能宣称 Proposed 已经更快或准确性已经达标。生产规划次数减少、固定 Seed 出现非零分歧，只是说明实验自变量终于真实存在；正式速度、TVD、轨迹误差和政策效应仍属于 Phase 6 Runner、Phase 8 Pilot/正式数据。

## 8. 仍待完成

项目作者已于 2026-08-17 确认本检查点。下一阶段只做：

1. 将阻塞式统一 Runtime 暴露为 `Initialize / StepHour / Finalize` 会话；
2. 建立最小 Backend 接口，逐步收拢当前 Runtime 内的方法分支，但不复制 Scenario/Action/Ledger；
3. 增加只读 Observer/Event Sink、Run Manifest、原始 CSV/JSONL 和离线指标重建；
4. 明确旧 Phase 2/3 Runner 只用于历史特征回归，正式数据只走统一生产会话；
5. 在性能采样中分开算法、Audit、Snapshot 和日志成本，并在有证据后再决定是否用稳定账户句柄/增量审计优化字符串 Ledger 查询。

Phase 7 UI/Actor、Phase 8 Pilot/正式实验，以及 480/90 次正式 Runs 均未开始。本检查点随 Phase 5.1 封板提交，未推送。
