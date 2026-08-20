# Hierarchical AI / Simulation LOD：MVP v1.8 正式实验前加固规则

**版本：v1.8**<br>
**日期：2026-08-20**<br>
**分支：`phase-6h-preformal-hardening`**<br>
**基线提交：`ef07f6c`（Phase 6G-B5E）**<br>
**状态：项目作者已采纳本加固方向；H0/H1/H2/H3 已以 `669bea6`、`bd9079f`、`52795a0`、`ab59954` 封板；H4 编译、2k 定向检查和 50k/100k 压力检查通过。任务计时信号仍保留到 H5 多 Seed 复核。**<br>
**用途：把“怎么量误差、怎么批量跑、怎么记录正式资格、怎么量内存”补牢。**

## 1. 先用一句大白话说明这次要做什么

v1.7 已经证明新 Proposed 可以运行，并且在人多时明显比 Per-Agent 快。现在不再改它“怎么模拟社会”，而是把实验用的尺子和流水线补完整：不能只说两名居民“一样或不一样”，还要说究竟差了多少钱、多少木材、多少小时；不能只按固定顺序跑一次，还要能保存固定乱序、重复编号和中断续跑；不能只看整个 UE 进程内存，还要知道模拟里的身份、群体和事件各占多少。

## 2. 本阶段绝对不改什么

H1—H5 不得修改以下模型规则：

- v1.7 的 Cohort 外层 Key、Joint Cell 维度和 Band 边界；
- Wait、Routine、Work、BuyWood、ChopWood、Repair 的选择和结算公式；
- Batch Claim 的竞争、分配、预约和完成规则；
- Lift/Restrict 的个人状态恢复公式、Capsule 内容和 Active 上限 50；
- Oracle、Per-Agent、Simple 的行为频率或资源规则；
- 两个王国、四个场景、地震、政策、时间线和 Day 60 语义。

如果 5 Seed 预检查发现误差越线，本阶段只记录和分析，不自动修改模型。任何模型调整都必须另开版本、说明影响并由项目作者再次批准。

## 3. H0—H5 怎么拆

| 检查点 | 要解决的实际问题 | 通过条件 |
|---|---|---|
| H0 | 先把规则写清楚，避免看完结果再换标准 | v1.8、实施计划和权威索引一致；不改代码 |
| H1 | “是否可用于正式实验”不应混进模拟世界结果 | 领域结果摘要不含正式资格；清单分别记录模型是否已批准、这一次是否请求正式运行、最终是否有效 |
| H2 | 现在只能看到“不一样率”，不知道究竟差多少 | 新增金钱、补助、木材和任务时间的大小误差；原始事件编号只作诊断 |
| H3 | 正式批量运行不能靠手工顺序和从头重跑 | 固定乱序、重复编号、唯一 RunID、运行清单、中断续跑、失败记录和无界面入口可测试 |
| H4 | 整个 UE 进程内存不能说明 Proposed 自己用了多少 | 分别记录身份、群体、Active、Capsule、Claim/Event、账本等已追踪内存 |
| H5 | 确认以上改动没有破坏结果，并检查多 Seed 误差 | 编译与完整回归通过；5 Seed × 4 场景的 Oracle/Proposed 预检查完成；不自动调参 |

## 4. H1：正式资格与结果摘要分开

### 4.1 三个不同问题必须分开记录

1. `formal_model_eligible`：这个方法版本是否已经完成工程与加固验收；
2. `formal_run_requested`：本次运行是否明确要求作为正式数据；
3. `valid_for_formal_experiment`：前两项都为真、没有硬错误，并且正式运行环境检查通过时才为真。

普通 Development Editor 工程测试即使结果正确，也必须保持 `valid_for_formal_experiment=false`。正式性能运行仍继承 v1.2：使用 Shipping；Development 只作开发和补充证据。

### 4.2 领域结果摘要只回答“模拟世界是否相同”

v1.7 新领域摘要版本固定为 `1.7-domain-v2`。它包含人口、资源、群体状态、事件、Capsule、时间和确定性顺序，但不得包含：

- 正式资格；
- Development/Shipping；
- Git 提交、机器、文件路径；
- CPU 时间、真实时间和内存。

因此，同一 Seed 和同一模型结果不能因为“这次叫工程测试还是正式运行”而得到不同摘要。摘要版本改变会让旧固定值统一变化；H1 必须逐项重算并说明这是摘要表头修正，而不是模拟行为改变。

## 5. H2：连续性误差要说明“差了多少”

保留既有字段不一致率，并新增以下主要指标：

| 大白话问题 | 指标 | 算法 |
|---|---|---|
| 每次观察平均差多少钱 | `MoneyMAE` | 所有配对快照的 `abs(Proposed-Oracle)` 平均值，单位 coin |
| 相对 Oracle 的钱差多少 | `MoneyNormalizedMAE` | 每项除以 `max(1, abs(Oracle))` 后求平均 |
| 维修补助平均差多少 | `RepairCreditMAE`、`RepairCreditNormalizedMAE` | 同上，单位 coin |
| 身上木材平均差多少 | `InventoryWoodMAE`、`InventoryWoodNormalizedMAE` | 同上，单位 wood |
| 房屋状态是否不同 | `HomeStateMismatchRate` | 不同配对数除以全部配对数 |
| 当前是否都在执行任务 | `TaskActiveStatusMismatchRate` | 一边有剩余任务时间、另一边没有的比例 |
| 做的是否是同一类事 | `CurrentGoalMismatchRate` | 当前动作类别不同的比例 |
| 同类进行中任务还差多久 | `TaskRemainingHoursMAE` | 双方都在执行同一类任务时，剩余小时绝对差的平均值 |
| 激活后的第一件事是否不同 | `FirstActionMismatchRate` | 保留现有口径 |

`EventIDMismatchRate` 和 `ReservationIDMismatchRate` 继续输出，但名称和说明必须明确标为“内部编号诊断”。批量事件和个人事件可以用不同编号，不能把编号不同直接写成玩家可见的不连续。

身份字段仍是硬门：PersistentID、HomeID、Kingdom、Profession 任何不一致都不是允许的近似。

## 6. H3：批量运行规则

- 默认兼容现有单次调用；重复次数为 1 时保留旧 RunID；
- 重复次数大于 1 时，RunID 必须追加 `R01`、`R02` 等编号；
- 所有待运行组合先形成 `run_schedule.csv`，记录最终顺序、方法、场景、Seed、重复编号和唯一 RunID；
- 顺序使用单独的 `order_seed` 做确定性乱序。同一个 order seed 重建的顺序必须完全相同，不得使用全局随机状态；
- `run_manifest.json` 必须记录协议版本、顺序位置、重复编号、是否请求正式运行及资格原因；
- 开启续跑后，只能跳过清单完整、输入 Hash/环境/RunID 相符且 `valid=true` 的已有 Run；不完整或不相符必须重跑或报错，不能静默当作成功；
- 失败写入独立失败记录，保留错误和 RunID；
- 提供可由 `UnrealEditor-Cmd` 调用的无界面入口，不需要打开编辑器逐项点击；
- 正式请求必须检查 Git 提交格式、构建类型和乱序配置；工程测试可以使用清楚标注的占位环境信息，但不得被标记成正式数据。

## 7. H4：内存口径

`memory_mb` 继续表示整个进程当前使用的物理内存，只能描述运行环境，不能写成“100k NPC 精确占用”。

v1.7 Proposed 另外在清单中记录以下已追踪内存：

- Identity Registry；
- Joint Cell 与索引；
- Active 个人状态；
- Continuity Capsule；
- Participant Ref；
- Batch Claim；
- Batch/System Event；
- Ledger；
- Reservation Store；
- Event Store 与 Scheduler；
- LOD 迁移记录；
- 以上项目合计。

统计必须包含容器分配和其中 String/Array 的额外堆内存。它仍不是操作系统级精确分摊，清单中必须写成 `tracked_authority_bytes`，不能伪装成整个模拟对象的绝对总内存。H4 只测量，不把 `TMap` 自动改成数组。

## 8. H5：5 Seed 连续性预检查

为了专门判断 Proposed 相对 Oracle 的个人连续性，本次固定：

- 总人口 200；
- 方法：Oracle、Proposed v1.7；
- 场景：None、HarvestCap、StateImport、RepairAid；
- Seeds：`20260810、20260811、20260812、20260813、20260814`；
- 共 40 Runs；
- Development Editor、NullRHI、Accuracy 模式；
- 这批只属于加固预检查，不进入正式统计。

以下是查看结果前固定的“停下来复核线”，不是自动调参按钮：

- 任意硬错误或身份不一致：立即停止，按实现错误调查；
- 任一 Seed/场景的 `MoneyMAE > 2 coin`；
- 任一 Seed/场景的 `RepairCreditMAE > 1 coin`；
- 任一 Seed/场景的 `InventoryWoodMAE > 1 wood`；
- 任一 Seed/场景的 `HomeStateMismatchRate > 0.10`；
- 任一 Seed/场景的 `FirstActionMismatchRate > 0.10`；
- 任一 Seed/场景的 `TaskActiveStatusMismatchRate > 0.10`；
- 同类进行中任务的 `TaskRemainingHoursMAE > 4 hours`。

越线只意味着需要读明细、判断是否为玩家可见或研究上重要的差异。未经项目作者确认，不修改 Cohort Band、Key、行动人数公式或恢复公式。

## 9. 每份检查点文档的写法

所有 H0—H5 检查点必须先回答：

1. 原来有什么具体麻烦；
2. 这一步实际改了什么；
3. 用什么例子能理解；
4. 检查了什么；
5. 能证明什么、不能证明什么；
6. 还有什么没完成。

必要技术名可以写在大白话解释后面，但不能只写“谱系、原子失败、逐步门、语义指标”等词而不解释。

## 10. 正式资格开放条件

H0—H5 全部通过后，v1.7 方法版本可以标记为 `formal_model_eligible=true`。但某一份 Run 只有在明确请求正式运行、环境检查通过且硬错误为 0 时，才写出 `valid_for_formal_experiment=true`。

本阶段完成不等于正式 480/90 Runs 已经完成，也不等于论文假设已经得到统计证明。
