# AILOD MVP Phase 7-0 检查点：冻结可视化规则和协作边界

**日期：2026-08-23**<br>
**分支：`phase-7-visual-demo`**<br>
**HEAD：`a9385b2`（本阶段没有代码提交）**<br>
**模型版本：`[FROZEN] v1.9`**<br>
**Demo 协议：v2.0**<br>
**状态：文档工作完成，等待项目作者确认；未确认前不得进入 Phase 7A。**

## 1. 一句话说明这一步完成了什么

这一步没有制作地图或 NPC，也没有写功能代码。它把“v1.9 后台模拟怎样安全接到地图、PCG、NPC、望远镜和 Dear ImGui”写成了正式可视化规则，并明确了以后每一步 Codex 做什么、作者做什么。

## 2. 本阶段实际完成的内容

### 2.1 Git 现场

- 从准确基准 `a9385b2` 创建并切换到 `phase-7-visual-demo`；
- 没有 reset、stash 或丢弃原文件；
- 没有 commit；
- 没有 push；
- 原来尚未跟踪的 Phase 7 交接文档被保留并补充，没有覆盖其他用户工作。

### 2.2 文档

- 新增 `Docs/AILOD_MVP_Prototype_Phase_Acceptance_Spec_CN_v2.0.md`；
- 更新 `Docs/AILOD_MVP_Current_Rules_Index_CN.md` 到索引 5.0；
- 补充 `Docs/AILOD_MVP_Phase7_Visual_Demo_Handoff_CN_v1.0.md`；
- 新增本检查点 `Docs/AILOD_MVP_Phase7_0_Checkpoint_CN.md`。

### 2.3 已写入 v2.0 的确认项

1. v2.0 只是 Demo/Presentation 协议；模型仍是 v1.9，领域摘要仍是 `1.9-domain-v1`；
2. 正式固定 Activation Trace 与互动镜头互斥；
3. 第一版互动 Demo 只运行 Proposed v1.9 / StateImport；方法或场景切换必须重开；
4. UI、Actor、动画和 Blueprint 只能读取复制出来的快照；控制只能发送有类型请求；
5. Dear ImGui 用于功能 UI，先做只读接口，之后才验证并固定 UE5.4 适配插件；
6. 左键地面移动、左键 NPC 选择、WASD 自由相机、F1 回到玩家；
7. 普通 Active 约 35、望远镜最多 5、缓冲约 10，全局硬上限仍为 50；
8. 允许少量绑定真实 ResidentID 的 Cohort 轻量代理；只有成功 Lift 后才使用完整 Active NPC Actor 和精确领域动作；
9. 望远镜中心代理持续被观察后提升；第一版保留一个当前追踪居民；
10. Actor 和地图卸载后，追踪居民仍保留同一 ResidentID、Active 名额和独立展示空间连续性；
11. 固定 Visual World Layout 同时供 NPC 空间目录和 PCG 使用；NPC 不扫描 PCG Actor 判断世界事实；
12. 作者只手工画少量道路、区域和重要地标；PCG 在编辑器中固定生成并保存房屋/树木；
13. World Partition 负责运行时加载/卸载，HLOD 负责远景，第一版不用运行时 PCG 重建王国；
14. Day -7 到 Day 0 在加载阶段预热，Day 60 停止并显示结果；
15. 初始 1x 为每真实秒 1 个游戏小时，每帧最多一个 StepHour；
16. 2k 用于开发，默认可视化使用 20k，100k 只是可选压力档；
17. 每个 Phase 7 检查点都必须写“完成什么、下一步 Codex 做什么、作者做什么”，需要作者操作 UE 时必须给出逐步部署说明。

## 3. 本阶段明确没有做什么

- 没有修改 `Source/`；
- 没有修改 `Content/` 或任何 `.umap/.uasset`；
- 没有安装或下载 Dear ImGui/UE 插件；
- 没有启用 PCG、World Partition、HLOD、NavMesh 或新模块依赖；
- 没有创建 Demo 地图、PCG Graph、空间格子、NPC 代理或 Actor 池；
- 没有编译，也没有重新运行 44 项自动检查；
- 没有运行 Demo、Pilot 或正式实验；
- 没有调整 v1.9 `[FROZEN]` 行为规则、Hash 或 Digest。

因此本检查点不能证明 PCG、地图、望远镜、UI 或 NPC 已经能运行，也不能证明有画面时的帧率。它只能证明实现边界和验收顺序已经写清楚。

## 4. 为什么推荐“固定布局＋编辑器 PCG＋流式加载”

大白话说：PCG 帮作者批量摆房子和树，World Partition 帮运行时决定哪些已经摆好的东西现在需要加载。

NPC 不能把“当前有没有加载房屋 Actor”当作世界事实。否则玩家一离开，房屋被卸载，NPC 就会认为房屋不存在。正确做法是 NPC 空间目录和 PCG 都读取同一份固定布局；地图卸载只影响画面，不影响 ResidentID、HomeID、任务和空间映射。

第一版把 PCG 结果生成后保存，可以避免运行时生成、碰撞和导航重建造成的额外风险；以后确认确实需要动态世界时，再单独评估 Runtime PCG。

## 5. 为什么新增低层代理

原来的“所有可见 NPC 都必须已经 Active”无法表现“玩家先看到远处一个低层居民，望远镜观察久了才提升”。

新规则增加一种轻量代理：

- 它绑定真实 ResidentID，所以不是假人；
- 它仍属于 Cohort，所以只做不承诺精确行动的轮廓/站立/慢走表现；
- 望远镜持续观察后，才请求 Lift 同一 ResidentID；
- Lift 成功后换成完整 Actor，并显示 v1.9 的真实身份、房屋、动作和任务；
- 第一版只追踪一个目标，避免无界占用 50 人名额。

这能直观展示 Simulation LOD，同时不要求 20k 居民都有 Character、路径和逐帧位置。

## 6. Phase 7A 下一步 Codex 要做什么

只有作者确认本检查点后，Codex 才进入 Phase 7A，并且只做无画面连接层：

1. 先重新编译当前基线并运行完整 `AILODResearch` 自动检查，记录进入 Phase 7 前的真实基线；
2. 增加明确的互动 Demo 模式，显式锁定 Proposed v1.9；
3. 保证互动模式不执行正式固定 Activation Trace；
4. 暴露最小的原子观察/释放/追踪请求；
5. 暴露 UI 和 Actor 需要的只读展示快照；
6. 增加命令记录、回放和失败不留半套状态的自动检查；
7. 对比正式模式的领域结果，确认 v1.9 未被改变；
8. 生成 `Docs/AILOD_MVP_Phase7A_Checkpoint_CN.md` 并再次停下来。

Phase 7A 不接 Dear ImGui、不创建地图、不做 PCG、不生成 NPC Actor。

## 7. 项目作者现在要做什么

本阶段不需要打开 UE 编辑器，也不需要编辑地图。现在只需要检查并确认：

1. v2.0 是否准确表达“模型仍是 v1.9，Phase 7 只增加展示连接”；
2. 是否接受 Cohort 轻量代理到 Active 完整 Actor 的提升过程；
3. 是否接受第一版只保留一个望远镜追踪居民；
4. 是否接受固定布局同时供 NPC 与 PCG 使用，NPC 不反向扫描 PCG Actor；
5. 是否接受第一版 PCG 在编辑器生成并保存，运行时主要由 World Partition/HLOD 加载；
6. 是否允许进入 Phase 7A 的无画面代码实现。

如果同意，请明确回复“确认 Phase 7-0，进入 Phase 7A”。在收到这句话前，不进入代码实现。

## 8. 作者以后需要怎样参与地图部署

作者第一次实际操作地图预计在 Phase 7C，而不是现在。届时 Codex 必须先提供一份可以照着执行的说明，至少包括：

- 怎样在 UE5.4 检查并启用 PCG 插件；
- 怎样创建独立 World Partition Demo 地图；
- 每个资产使用什么固定名称和保存目录；
- 怎样画第一条道路样条线；
- 怎样建立住宅区和森林区；
- 怎样放置林场、木材采购点和住宅展示点；
- 怎样连接 PCG Graph、填写固定 Seed 并生成房屋/树木；
- 怎样保存/Bake 结果；
- 怎样检查 World Partition 格子加载与卸载；
- 怎样生成或检查 HLOD；
- 怎样确认道路能走、建筑不会堵死导航；
- 每一步预期看到什么，以及不符合时在哪里停止并截图反馈。

在 Phase 7A、7B 没有通过以前，不要求作者提前摸索这些功能，避免地图资产和尚未冻结的数据接口互相返工。

## 9. 保护 v1.9 的检查结果

- `[FROZEN]` Cohort、竞争、资源、动作、HomeID 和维修语义：未修改；
- UI/Actor 第二权威：文档明确禁止；
- 正式固定轨迹与互动镜头同时运行：文档明确禁止；
- 每帧全人口扫描：文档明确禁止，并要求空间格子与候选访问计数；
- Demo 数据混入正式实验：文档明确禁止；
- 20k/100k Actor 或逐居民路径：文档明确禁止；
- 未经确认连续完成 Phase 7：文档明确禁止；
- push：未执行，也未获得授权。

## 10. 当前停止点

Phase 7-0 的文档产物已经形成，但尚未由项目作者验收，也没有提交。当前必须停在这里，等待作者检查 v2.0、规则索引、交接补充和本检查点。
