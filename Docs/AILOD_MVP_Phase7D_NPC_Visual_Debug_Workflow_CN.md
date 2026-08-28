# AILOD MVP Phase 7D：NPC 可视化 Debug 流程

**日期：2026-08-26；状态更新：2026-08-29**
**分支：`phase-7-visual-demo`**
**适用范围：Phase 7D NPC 出现、消失、换区、代理和 revisit 连续性问题**
**当前状态：备用回归流程，当前不执行。合法 revisit 事务键修复后，项目作者已在正式主地图 PIE 确认返回同一地点时 NPC 不再消失；只有问题再次出现才启动 D0—D6。**
**权威模型：`[FROZEN] v1.9`**

## 1. 这次调试要解决什么

本流程建立时的最大人工可见问题是：玩家移动或离开再回来时，NPC 会整批出现、整批消失，旧区域的人可能不再回来，`deferred` 也可能长期保留。后来确认其中一个根因是 Pause 下同一分钟合法 Lift/Restrict revisit 使用了已经提交的事务键；唯一键修复后，项目作者已在正式主地图 PIE 确认该消失问题不再复现。

若问题以后再次出现，不在正式大地图上继续同时调五六套系统，而是建立一个最简单的 200 人测试场景，每次只增加一层功能：

```text
200 个身份稳定可见
    ↓
固定一批 Active，完全不换人
    ↓
手动切换 Active 集合
    ↓
让镜头位置决定 Active 集合
    ↓
加入低层代理和 LOD 切换
    ↓
回到正式 20k 地图验证
```

哪一步第一次失败，Bug 就主要在这一层或这一层与上一层的连接处。

## 2. 先统一四个词

- **居民身份（Identity）**：模拟中真实存在的居民，拥有稳定 `ResidentID`。不等于当前有完整 NPC Actor。
- **身份标记（Identity Marker）**：Debug 地图里用来表示“这个居民存在”的只读简单身体。它不是新的模拟对象。
- **Active**：正在进行详细模拟、可以读取精确动作快照的居民。`[FROZEN] v1.9` 规定最多 50 个。
- **Proxy**：非 Active 居民的低成本画面代理。目前就是细圆柱一类的简化显示。

因此，第一步可以让 **200 名居民全部生成、全部在画面中有标记**，但不能把 200 人都称为 Active。把 200 人都改成真正 Active 会破坏 v1.9 的 50 人上限，也会绕开我们真正需要检查的 Lift、Restrict 和集合替换路径。

## 3. 不可跨越的调试边界

1. 不修改金币、木材、住房、职业、任务、事件、时钟或 Cohort 规则。
2. UI、Debug 标记、NPC Actor 和 Proxy 都只读模拟结果，不能成为第二套权威状态。
3. 同一个 `ResidentID` 同一时刻只能有一种身体表示，不能既有完整 Actor 又有 Proxy 或身份标记。
4. Active 永远不超过 50。
5. 不通过每帧扫描全部 20k/100k 居民来“修好”画面。
6. 一个阶段没有通过前，不加入下一层功能。
7. 正式地图、PCG、World Partition 和 HLOD 留到最后验证；前面的 Debug 阶段不依赖它们。
8. 每次修复只针对当前失败阶段，不顺便重写旁边系统。

## 4. Debug 地图的最小规格

建议新建：`/Game/Phase7/Tests/L_Phase7_NPCContinuityTest`

- 使用 v1.9 已支持的最小人口：每个王国 100 人，共 200 人；
- 使用固定 Seed；
- 一块平地，两个简单道路区域 A、B；
- 200 个固定位置分成 4 组，每组 50 人；
- 不使用 PCG、HLOD、World Partition 流送或 AI 寻路来生成居民；
- 保留简单相机移动和点击选择；
- 所有居民数据仍来自正式的 Identity、Runtime 和 Presentation 链，不能制作一份假的 200 人数组冒充模拟结果。

四组居民不要假定 ID 一定连续。应把真实 `ResidentID` 稳定排序后，再依次分成 G0、G1、G2、G3，每组 50 人。

## 5. 画面和诊断信息

### 5.1 NPC 头顶信息

小地图中每个可见居民显示一行只读 Debug 标签：

```text
ResidentID | 表示状态 | 职业缩写
```

表示状态和颜色统一为：

| 标记 | 含义 | 建议颜色 |
|---|---|---|
| `I` | 只有身份标记 | 白色 |
| `A` | Active 完整 Actor | 绿色 |
| `P` | 低层 Proxy | 灰色 |
| `D` | 本次集合替换 deferred | 黄色 |
| `E` | 不变量失败或重复表示 | 红色 |
| `S` | 当前选中居民 | 青色描边或青色文字 |

职业只显示文字缩写，不再占用身体颜色。身体颜色优先表达表示状态，因为这才是当前 Bug 的核心。

标签应由一个 Debug 绘制层统一投影到屏幕，不给 200 人各创建一个长期存在的 TextRender Actor。

### 5.2 全局诊断表

画面上必须能查看当前关注居民的这条链：

```text
ResidentID | Candidate | Desired | Committed | ProxySlot | ActorSlot | LastReason
```

含义：

- `Candidate`：空间查询有没有找到他；
- `Desired`：本帧计划是否想把他设为 Active；
- `Committed`：权威 Runtime 是否已经接受；
- `ProxySlot`：是否占用低层代理槽；
- `ActorSlot`：是否绑定完整 Actor 槽；
- `LastReason`：最近一次进入、离开、拒绝或 deferred 的原因。

同时显示这些总数：

```text
Identity = 200
UniqueRepresentation = 200
Active <= 50
Proxy
IdentityMarker
DuplicateRepresentation = 0
MissingRepresentation = 0
FullPopulationScan = No
```

## 6. 分阶段 Debug 流程

### D0：记录当前正式地图故障

**目的：**先留下一个可以对照的真实失败样本，避免之后只凭感觉判断。

**做法：**

1. 在当前正式地图复现一次“这里原来有人，离开后回来变空”或长期 `deferred`；
2. 记录 Seed、游戏时间、速度、玩家/镜头所在区域；
3. 对一名消失居民记录诊断链七列；
4. 保存一张画面截图和对应日志片段。

**完成条件：**得到一次完整失败记录；如果连续 3 次都无法复现，也要记录 3 次结果后再进入 D1。

**本阶段不修复。**

---

### D1：200 个身份标记，完全没有 LOD

**只打开：**Identity → 固定位置 → Debug 身份标记。
**全部关闭：**Active 集合切换、Actor 池换绑、Proxy、HISM、空间选人、Streaming。

**做法：**

1. 固定游戏时间，不推进模拟；
2. 显示全部 200 个真实居民身份标记；
3. 移动和旋转镜头，走遍 A、B 两区；
4. 随机点击至少 10 人，核对标签 ID 与选择详情 ID；
5. 重复进入 PIE 3 次。

**通过标准：**

- 每次都是 200 个唯一 `ResidentID`；
- `MissingRepresentation = 0`；
- `DuplicateRepresentation = 0`；
- 镜头移动不会让任何一批身份标记消失；
- 同一 Seed 下位置和 ID 对应关系不变；
- 点击读到的 ID 与头顶 ID 一致。

**失败首先检查：**身份目录、固定布局、ID 到位置映射、基础 Presenter。此时还不能怪 Actor 池、Proxy 或 LOD。

---

### D2：固定 20 名 Active，不允许换人

**新增：**固定 20 名真实 Active、完整 NPC Actor。
**仍关闭：**空间选人、自动集合替换、Proxy、HISM。

其余 180 人继续显示为身份标记；20 名 Active 不再额外保留身份标记，确保一个 ID 只有一具身体。

**做法：**

1. 使用稳定排序后的前 20 个 ID；
2. 依次测试 Pause、1x、2x、4x；
3. 跨过至少三个 8 游戏小时边界；
4. 镜头离开 A 区再回来，但 Active 集合始终不变；
5. PIE 重启 3 次。

**通过标准：**

- 20 个 `ActorSlot` 绑定稳定，不无故释放或换绑；
- Active ID 不变、数量不变、永不超过 50；
- 没有整批跳位、消失或重新生成；
- Pause 时展示停止，1x/2x/4x 的展示速度正确；
- 每次重启同一 Seed 得到同一组 ID；
- 200 个身份仍全部可追踪，且没有重复身体。

**失败首先检查：**Active 快照到展示帧、完整 Actor 绑定、动画时间和固定 Actor 槽。

---

### D3：手动切换四组 Active，仍不看镜头

**新增：**手动 Active 集合替换。
**仍关闭：**空间查询、自动 LOD、Proxy、HISM。

**做法：**

1. 通过只读 Debug 控制依次提交 `G0 → G1 → G0`；
2. 再执行 `G0 → G1 → G2 → G3 → G0`；
3. Pause 状态先做 5 个完整循环；
4. 再在 1x 和 4x 各做 3 个循环；
5. 每次替换记录旧集合、目标集合、已提交集合和 Actor 槽变化。

**通过标准：**

- 每次替换要么完整成功，要么完整保持旧集合，不能半新半旧；
- 回到 G0 后，必须仍是同一批 G0 `ResidentID`；
- Actor 池释放数和绑定数与集合差集一致；
- 不出现重复 Actor、空槽幽灵或长期 `deferred`；
- 200 个身份没有被删除，只是表示状态改变；
- 同一请求不会因每帧重复提交而累计事务。

**失败首先检查：**Candidate Plan、原子提交、IdempotencyKey、旧集合释放、新集合绑定和拒绝回滚。

---

### D4：让镜头决定 Active，但暂时没有 Proxy

**新增：**空间格子查询、Candidate、Desired、Committed 链。
**仍关闭：**低层 Proxy 和 HISM；非 Active 仍显示身份标记。

**做法：**

1. Pause 后执行 A 区 → B 区 → A 区；
2. 对比第一次和第二次进入 A 区的 Candidate、Desired、Committed ID；
3. 沿边界缓慢来回移动，再快速横移；
4. 在 1x 和 4x 重复 A → B → A；
5. 记录每次替换发生的原因和时刻。

**通过标准：**

- Pause 下回到同一位置，选中的 ID 集合必须确定一致；
- Committed 只在 Runtime 接受后改变；
- 拒绝时完整保留旧 Committed 集合，不能出现半提交；
- 非 Active 身份标记始终存在，因此不会出现整片道路突然无人；
- 没有每帧全人口扫描；
- Active 始终不超过 50。

**失败首先检查：**空间查询稳定排序、滞回输入、Candidate/Desired/Committed 分层，以及同一游戏分钟重复请求。

---

### D5A：加入静态 Proxy，但先不切 LOD

**新增：**用 Proxy 替代非 Active 身份标记；镜头和 Active 集合暂时固定。

**做法：**固定镜头观察 2 分钟，选择不同 Proxy，检查 ID、槽位和点击结果。

**通过标准：**

- 一个非 Active ID 正好对应一个 ProxySlot；
- 一个 Active ID 只对应一个 ActorSlot，不再有 ProxySlot；
- Proxy 不闪烁、不重号、不残留；
- 点击 Proxy 只读取信息，不自动把居民提升为 Active。

**失败首先检查：**Proxy 槽映射、HISM Instance Index 到 ResidentID 映射和增量更新。

---

### D5B：真正启用 LOD、边界滞回和 revisit

**新增：**镜头移动驱动 Active/Proxy 切换和滞回。

**做法：**

1. Pause 下执行 A → 边界 → B → A，重复 5 次；
2. 贴着边界小幅来回移动 30 秒；
3. 快速从 A 横移到 B，再立即返回 A；
4. 在 1x 和 4x 各重复 3 次；
5. 追踪至少 5 个固定 `ResidentID` 的完整状态链。

**通过标准：**

- Pause 下 revisit 后仍能找到同一个居民 ID；
- 边界小幅移动不会让同一人高频 A/P/A/P 闪烁；
- 切换时一个 ID 始终只有一种表示；
- 不发生整批空白、停下后才整批出现或旧 Proxy 残留；
- `deferred` 必须有明确原因，并能在条件解除后恢复，不能永久卡住；
- 1x/4x 只改变时间推进速度，不改变同一时刻、同一位置的确定性结果。

**失败首先检查：**滞回历史、代理槽稳定性、集合提交节流、deferred 恢复和 revisit 身份连续性。

---

### D6：回到正式 20k 地图

只有 D1 到 D5B 全部通过后，才把同一套生产代码放回 `/Game/Phase7/Maps/L_Phase7_VisualDemo`。

**做法：**

1. 不先改 PCG、HLOD、NavMesh 或 World Partition 参数；
2. 复现 D0 的相同路线；
3. Pause 下做 A → B → A；
4. 再在 1x 和 4x 做相同路线；
5. 观察 World Partition 加载边界附近的 NPC 连续性；
6. 对比 D0 的失败记录。

**通过标准：**

- 小地图已通过的 ID、Actor、Proxy 和 deferred 不变量在正式地图仍成立；
- 远近区域没有整批消失或整批延迟冒出；
- 返回旧区域时居民身份连续；
- `FullPopulationScan = No`；
- PCG/World Partition 只影响地图资源加载，不改变居民权威身份。

如果只有 D6 失败，优先检查 World Partition、Streaming Source、地图坐标或内容加载；不要回头改已通过的模拟身份和 Actor 池规则。

## 7. 症状与责任范围速查

| 第一次失败阶段 | 首要责任范围 |
|---|---|
| D1 | 身份、布局、固定位置或基础绘制 |
| D2 | Active 快照、Actor 绑定或展示时间 |
| D3 | 原子集合替换、事务幂等或 Actor 池换绑 |
| D4 | 空间查询、Candidate/Desired/Committed 连接 |
| D5A | Proxy 槽、HISM Instance 与 ResidentID 映射 |
| D5B | LOD 滞回、快速移动、deferred 恢复、revisit |
| 只有 D6 | World Partition、Streaming、正式地图坐标或内容加载 |

## 8. 每一步怎样记录结果

本次只保留这一份流程文档。之后每次调试结果不再新建文档，直接在当前 Codex 会话按下面格式汇报：

```text
阶段：D?
本轮只新增的功能：
运行次数：
关键计数：Identity / Active / Proxy / Missing / Duplicate
观察结果：
失败 ResidentID 与七列诊断链：
结论：通过 / 未通过
下一步：继续本阶段修复 / 获准进入下一阶段
```

自动测试通过不等于人工画面通过。每个阶段都需要：

1. Codex 完成代码审查、自动检查和日志核对；
2. 项目作者只做该阶段列出的短 PIE 验收；
3. 双方确认通过后，才进入下一阶段。

## 9. 整个 Debug 何时算完成

至少满足以下条件：

- 200 个真实居民身份始终唯一、可追踪；
- 固定 Active 不会自行消失或无故换绑；
- 手动集合替换是原子的，A → B → A 能回到同一批 ID；
- Pause 下同位置 revisit 的结果确定一致；
- Active、Proxy 和身份标记之间没有重复身体或空白窗口；
- 快速移动和边界徘徊不造成整批闪烁；
- deferred 有明确原因并能恢复；
- 正式 20k 地图复测通过，且仍无全人口逐帧扫描。

这只能证明 Phase 7D 的可视化身份与 LOD 切换链稳定，不能代替 Phase 7F 的正式帧时间、内存、显存、流送尖峰和论文实验数据。

## 10. 启用本备用流程时的执行顺序

当前不执行 D0—D6，也不为了“预防可能复发”提前建设 200 人 Debug 小地图；项目下一步按已批准路线进入 Phase 7E。

只有主地图再次出现整批消失、长期 `deferred`、重复身体、Actor/Proxy 错号或 revisit 不连续时，才启动本流程：先只做 **D0** 留下失败样本，D0 完成后再实现和验收 **D1**；在 D1 通过以前不开始 D2，也不继续修改正式地图上的 Proxy 或 LOD。
