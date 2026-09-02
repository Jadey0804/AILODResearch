# AILOD MVP 剩余重点工作交接 v1.0

**日期：2026-08-30**<br>
**项目目录：`C:\WarwickProjects\AILODResearch`**<br>
**UE：5.4.4，安装目录 `D:\ruanjian\Unreal Engine\UE_5.4`**<br>
**当前分支：`phase-7-visual-demo`**<br>
**当前 HEAD：`698b8a19ed4d7878d9a9289b00937b3433505090`**<br>
**Phase 7E 提交：`96bd6a3`**<br>
**推送状态：以上提交均为本地提交，尚未 push。**

## 1. 这份文档用于什么

这份文档交给新的 Codex 对话继续完成毕设收尾。目标不是继续扩展游戏内容，而是按顺序完成：

1. Phase 7F 可视化收口；
2. 正式实验和最终图表；
3. 用真实结果完成论文和答辩材料。

新对话必须先只读核对现场，再提出当前一个小步骤的方案。未经项目作者确认，不得同时跨越多个阶段实施。

## 2. 新对话的必读顺序

1. 本文件；
2. `Docs/AILOD_MVP_Current_Rules_Index_CN.md`；
3. `Docs/AILOD_MVP_Current_Project_Status_Plain_CN.md`；
4. `Docs/AILOD_MVP_Prototype_Phase_Acceptance_Spec_CN_v1.9.md`；
5. `Docs/AILOD_MVP_Prototype_Phase_Acceptance_Spec_CN_v2.0.md`；
6. `Docs/AILOD_MVP_Phase7D_Checkpoint_CN.md`；
7. `Docs/AILOD_MVP_Phase7E_Checkpoint_CN.md`；
8. 准备正式实验时再读 `Docs/AILOD_MVP_Prototype_Implementation_Spec_CN.md` 和 Phase 6H、6I 检查点；
9. 写论文时读取 `Deliverables/Thesis/NPC_AI_Simulation_LOD_Graduation_Thesis_v1.0_CN.docx`。

发生冲突时，模拟领域以 v1.9 和当前规则索引为准；可视化边界以 v2.0 为准。旧交接和旧检查点只作为历史证据，不能覆盖新版本。

## 3. 当前已经完成什么

### 3.1 模拟和实验底座

- v1.9 大规模 NPC 社会模拟已完成工程验收并获得正式实验资格；
- ResidentID、HomeID、房屋状态、进行中任务和预约能够在聚合、解聚合、离开和回访后保持连续；
- 20k 默认规模与 100k Proposed 工程压力检查已跑通；
- 已有固定 Seed、运行清单、回放、失败记录、硬错误检查、准确性指标和性能指标；
- Development/Pilot 证据只证明工程可运行，尚未变成论文正式统计。

### 3.2 Phase 7-0 到 7E

- `a092164`：Phase 7-0 可视化规则；
- `4f642a9`：Phase 7A 无画面互动连接层；
- `5621303`：Phase 7B 固定空间布局与确定性候选；
- `611534a`：Phase 7C 地图、PCG、World Partition、HLOD、导航、相机和 UI；
- `4209962`：Phase 7D 真实 ResidentID 代理、最多 50 个完整 Actor、点击读取和 revisit 修复；
- `96bd6a3`：Phase 7E 望远镜、远距加载、权威 Lift、同一 ResidentID 追踪，以及 PCG 新房屋、工厂和三种树。

项目作者已经在主地图确认：

- 道路、移动、碰撞和 NavMesh 可用；
- NPC revisit 后不再整批消失；
- 1x/2x/4x 与 NPC 移动同步；
- 望远镜能够进入近地视角并 Lift 远处真实居民；
- PCG 道路正常，房屋、工厂和树木已经完成第一轮模型替换与贴地调整。

### 3.3 已有验证，但不要夸大

- Phase 7D 的较早版本通过 Editor/Game Development、8/8 专项和当时的 66/66 完整回归；后续唯一事务键修复只重新编译 Editor，并由作者完成主地图 revisit 验收；
- Phase 7E 通过 Editor Development 和 1/1 专项检查，并完成主地图人工验收；
- 最新 PCG 离线强制生成修改 85 个包，命令行结果为 0 error；
- 最新 HEAD 尚未执行 Phase 7F 完整回归、Game Development、Cook/Package 或有画面性能测量；
- NullRHI 和以前的无画面计时不能作为 FPS、GPU、显存或 World Partition 加载性能证据。

## 4. 当前现场和已知问题

### 4.1 UE 资产

- 主地图：`/Game/Phase7/Maps/L_Phase7_VisualDemo`；
- PCG Graph：`/Game/Phase7/PCG/PCG_Phase7_Layout_v1`；
- PCG Volume：`PCGV_Phase7_Layout`；
- 房屋：`/Game/Resourses/House/h1`；
- 工厂：`/Game/Resourses/Factory/old_wooden_barn_house`；
- 树木：`SM_FreeTree_04`、`SM_FreeTree_05`、`SM_FreeTree_07`；
- PCG 已重新生成，但新模型对应的 HLOD 尚未重建。

不要在近景 PCG 和模型比例尚未最终确认前反复 Build HLOD。HLOD 应在资产稳定后只重建一次并验收远景。

### 4.2 三个主要表现问题

1. 低层 NPC 目前是静止细圆柱，而完整 NPC 是圆柱身体加球形头，玩家明显能看出 LOD 切换；
2. `Telescope Camera Pitch Degrees=-5` 后，屏幕准星和实际中心候选的选择方向轻微不一致；
3. PCG 新模型没有对应的新 HLOD，远处分区仍可能显示旧轮廓。

### 4.3 Git 工作树

交接文档创建前，所有已跟踪改动均已提交。以下两个目录是本地未跟踪、当前地图未引用的素材，不能擅自删除、移动、提交或隐藏：

- `Content/Fab/Lowpoly_Trees/`；
- `Content/Fab/Old_Wooden_Barn_House_4_/`。

本交接文件本身预计也是未提交文件。新对话第一次核对 `git status` 时，应看到上述现场；不得 reset、clean、stash 或覆盖。

## 5. 剩余工作必须按这个顺序进行

## 5.1 Phase 7F-0：只读审计与验收矩阵

第一步不改代码、不改 UE 资产、不跑大批测试。只做：

1. 核对分支、HEAD、工作树和上述必读文档；
2. 阅读代理/Actor 池、望远镜选择和设置代码；
3. 把 Phase 7F 拆成下面 7F-A 到 7F-E；
4. 对低层代理方案进行对抗性评估，给出最小可行方案、风险、性能成本和人工验收方法；
5. 等待项目作者批准后才开始实现。

## 5.2 Phase 7F-A：解决视觉 LOD 穿帮

这是当前最高优先级的代码问题，也是演示可信度的必要条件。

目标不是让全部 20k 居民都变成完整 Character，而是：

- 低成本代理和完整 Actor 在正常观察距离下具有相近的人形轮廓；
- 低层代理具有便宜、连续的行走表现，而不是静止木棍；
- 玩家靠近、点击或望远镜 Lift 时，同一 ResidentID 在同一位置无感升级；
- 降级后继续从同一位置和路线进度显示，不闪现、不复制、不突然消失；
- 完整 Actor 仍最多 50 个；代理仍有独立预算；不得每帧扫描 20k 人；
- 代理只能表现站立/慢走等非精确动作，不能伪装成正在维修、砍柴等权威行为。

不要预先假定一定使用 HISM、Mass、AnimToTexture 或 Skeletal Mesh。新对话应先检查当前 NPC 资产和现有渲染结构，再选择最简方案。不得为了外观问题修改 v1.9 模拟公式或居民状态。

最小人工验收：玩家缓慢接近、快速穿过、离开后返回、点击居民、望远镜 Lift，同一 ResidentID 都不出现明显跳位、双人重叠、木棍变人或整批闪烁。

## 5.3 Phase 7F-B：修正准星与候选方向

目标是让“屏幕中心看到谁”和“系统选择谁”使用同一条相机方向。不能通过为当前 `-5°` 单独写死补偿完成。

验收要求：

- 调整 Telescope Height、Pitch 和 FOV 后，中心准星仍与候选选择一致；
- 遮挡物后面的 NPC 不应被当成明显可见目标；如果第一版不做遮挡检测，必须在限制中明确说明；
- 不改变 300—1500 m、持续观察、Streaming Ready 和最多 50 Active 的既有规则。

## 5.4 Phase 7F-C：稳定 PCG 后重建 HLOD

只有 7F-A、7F-B 和近景 PCG 验收都稳定后才做：

1. 保存并关闭 UE；
2. 核对 PCG 新模型和分区包；
3. 重建当前地图 HLOD；
4. 验证近处使用新房屋/工厂/三种树，远处分区卸载后仍有正确轮廓；
5. 验证道路、NavMesh、碰撞和 PIE 加载没有回退。

HLOD 不负责 NPC Simulation LOD，也不能用来修复居民身份或代理切换。

## 5.5 Phase 7F-D：一次完整正确性关闭

代码和 UE 资产冻结后再统一运行，避免每个小改动都跑一堆无价值测试：

- `AILODResearchEditor Win64 Development`；
- `AILODResearch Win64 Development`；
- Phase 7F 新增的最小专项；
- 全部 `AILODResearch` 自动回归一次；
- 验证 v1.9 正式模式摘要、Digest、硬错误门、Active 上限和固定实验语义未变；
- 必要时做 Cook/Package 冒烟。

如果只是改 PCG 模型、HLOD 或相机配置，不要重复运行与其无关的大批领域测试。完整回归在 Phase 7F 最终冻结点运行一次。

## 5.6 Phase 7F-E：有画面性能与最终检查点

测量对象：

- 2k 开发档；
- 20k 默认 Demo；
- 100k 仅在 20k 稳定后作为可选压力，不作为默认演示承诺。

至少覆盖：

- 静止普通镜头；
- 快速移动和旋转镜头；
- 代理密集区域；
- 50 个完整 Actor；
- World Partition 加载/卸载；
- 望远镜反复开启、远距加载、Lift 和清除；
- 1x 与 4x。

记录：基本 FPS、Frame/Game/Draw/GPU 时间、P50/P95/P99 或可复核的帧时间分布、加载尖峰、Draw Call、进程内存和显存现象。原始日志必须保留，不能只抄一张平均 FPS 截图。

最终生成 `Docs/AILOD_MVP_Phase7F_Checkpoint_CN.md`，明确：能证明什么、不能证明什么、已知限制、演示步骤和论文允许使用的结论。作者验收后再提交；未经要求不 push。

## 6. Phase 7F 之后：正式实验

正式实验必须与互动 Demo 完全隔离，使用冻结的 v1.9、固定 Activation Trace 和正式运行入口。不得让相机、望远镜、PCG、Actor 或 Demo 命令进入正式数据。

冻结矩阵：

- 小规模准确性：200 人，Oracle / Proposed / Simple / Per-Agent，4 个场景，30 个配对 Seed，共 480 Runs；
- 大规模性能：2k / 10k / 20k，Proposed / Simple / Per-Agent，每个配置 10 Runs，共 90 Runs；
- Oracle 不在大规模运行；
- 50k/100k 只属于 Proposed 工程压力证据，不冒充正式大规模四方法比较；
- 正式性能使用 Shipping；Development 只作为开发和补充证据；
- 每个正式 Run 必须明确标记正式资格、保持硬错误为 0，并保留 manifest、配置、输入指纹、日志和失败记录；
- 先做一次只验证环境的极小预运行，确认输出目录、磁盘空间、续跑和统计脚本，再启动 480/90，不得边跑边改参数。

正式实验完成后输出可复核的 CSV、汇总表、图表、置信区间和论文结论。不要从截图手抄数据。

## 7. 最后：论文与答辩收尾

论文 v1.0 已有 IMRaD 框架和初稿，但不能把占位数字当结果。正式数据产生后再完成：

1. Results：客观放入准确性、连续性、性能、内存和切换错误数据；
2. Discussion：解释性能收益、近似误差、视觉 LOD 意义、限制和适用范围；
3. Conclusion：只回答研究问题，不宣称 100k 完整 NPC Actor 或商业游戏完成；
4. 图表、编号、交叉引用、术语、参考文献和附录；
5. 可复现步骤、硬件与 UE/构建配置；
6. 最终 DOCX/PDF 视觉检查和答辩演示脚本。

## 8. 必须遵守的硬边界

- 不为修画面修改 `[FROZEN] v1.9` 的金币、木材、住房、行动、政策、事件或时间语义；
- UI、Actor、代理、PCG、World Partition 和 HLOD 只能读取展示数据，不能直接写领域状态；
- 互动镜头/望远镜与正式固定 Activation Trace 不能同时运行；
- 不生成 20k/100k 个 Actor、Character、逐居民 Tick 或逐居民运行时寻路；
- 所有代理和完整 Actor 必须绑定真实 ResidentID；
- 不把 NullRHI 时间当有画面性能；
- 不把工程 Pilot 当论文正式结果；
- 不 reset、clean、stash、删除或覆盖用户未跟踪素材；
- 不擅自 push；
- 每次只完成当前获批小步骤，完成后在当前对话说明证据和人工验收点，不为每轮小调试重复创建新文档。

## 9. 新对话第一条指令

将下面整段复制到新的 Codex 对话：

```text
项目目录是 C:\WarwickProjects\AILODResearch。请完整阅读 Docs/AILOD_MVP_Remaining_Work_Handoff_CN_v1.0.md，并按其中“必读顺序”核对当前规则和检查点。

这一步只做 Phase 7F-0 只读审计：核对 branch、HEAD、git status、Phase 7E 提交、当前源代码和 UE 资产引用。不要修改代码或资产，不要创建分支，不要提交，不要 push，不要运行整套自动测试，也不要处理或删除两个未跟踪素材目录。

然后用大白话给我：1）当前事实是否与交接一致；2）低层细圆柱代理为什么会穿帮；3）结合现有代码和资产提出最简、最稳、性能可控的无感 LOD 方案；4）Phase 7F-A 的精确修改范围、验收标准和风险。对抗性思考，不要默认 HISM、Mass 或 AnimToTexture 一定正确。停下来等我批准后再实施。
```

## 10. 当前总判断

当前整个毕设约完成 70%。最难的模拟和实验基础已经具备；真正决定能否答辩的剩余项是：可视化不穿帮、Phase 7F 有画面证据、正式 480/90 Runs，以及用真实数据完成论文。下一步应收口，不应继续增加新玩法。
