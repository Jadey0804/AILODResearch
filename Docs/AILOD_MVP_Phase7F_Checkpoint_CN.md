# AILOD MVP Phase 7F 检查点（已封板）

日期：2026-08-31
历史分支：`phase-7-visual-demo`
Phase 7F 封板提交：`3314034d`（`Phase 7F-E: capture visual performance and fix NavMesh capacity`）
当前后续分支：`phase-8-formal-experiments`
状态：Phase 7F 已由作者批准并封板。`formal1` 24 格 Development 有画面工程数据、NavMesh 修正后的 `navfix1` World Partition 4 格、采样代码、工具、NavMesh 资产和本文档均已提交；尚未 push。

## 1. 先说结论

Phase 7F 已经拿到一套可复核的有画面工程证据。`formal1` 一共包含24格：6类场景、2k/20k、1x/4x，每格预热15秒并录制30秒。NavMesh fixed tile pool问题修正后，按作者批准的最小范围只重跑了 World Partition 4格，run-set为`navfix1`。原24格和新增4格都生成了CSV、UE日志和JSON manifest；人口、倍率、场景动作和`FullPopulationScan=0`都能从原始数据复核。

普通静止镜头、代理密集镜头和44个完整Actor绑定时，当前机器上的帧时间相对稳定。快速移动会增加尾部波动。World Partition往返和望远镜远距加载仍会产生明显尖峰。NavMesh修正后最重的一格仍是20k、4x、World Partition往返：Frame P50/P95/P99为`16.10 / 33.11 / 52.40 ms`，最大`121.57 ms`；Game Thread P99为`52.26 ms`，GPU P99为`20.91 ms`。

当前有两个必须写清楚的工程事实：

1. 原Recast NavMesh运行期固定tile pool只有288。作者批准后已把池提高到32768并重建分区NavData。`navfix1`四份运行日志的`Failed to add tile`均为0，说明已解决本次测试路线暴露的tile attach容量问题。该证据仍不等于逐点验证整张地图的可达性。
2. 50是完整Actor池容量。冻结配置允许的普通Active上限为44，望远镜上下文预算为5，并保留额外安全余量。正式矩阵里的实际同时绑定峰值为44，`actor_cap_reached=False`。作者已批准统一使用“50槽池、44个实际绑定”的准确口径。

因此，Phase 7F-E 的采样链、原24格工程矩阵和NavMesh修正后的4格复测已经完成。20k/4x的World Partition尖峰有所下降，但Game Thread仍会出现明显长帧。100k可选压力测试暂不执行。

## 2. Phase 7F 各子阶段的实际状态

### 2.1 Phase 7F-A：视觉 LOD

提交：`0b1ec08 Phase 7F-A: smooth proxy and active visual LOD`

已实现：

- 低层代理改成身体加头部的低成本人形轮廓；
- Proxy 和完整 Actor 共用同一 ResidentID、路线进度和位置来源；
- 普通观察加入进入停留时间和离开宽限，减少频繁升降级；
- 点击、靠近和望远镜 Lift 继续走现有权威 Active 请求；
- Proxy 使用有上限的 HISM 槽位和轻量移动，完整 Actor 使用固定池；
- 正式矩阵全部记录 `FullPopulationScan=0`。

项目作者此前批准“7F-A 先这样”。本轮定向自动回归 `AILODResearch.Phase7FA` 为 5/5 Success。该结果覆盖规划与连续性代码，现场主观穿帮程度仍以作者最终观看为准。

### 2.2 Phase 7F-B：准星与候选方向

项目作者明确决定先跳过 7F-B。当前实现继续使用相机方向选择望远镜候选，未加入遮挡检测。墙后或建筑后的居民仍可能成为候选；Height、Pitch、FOV 任意变化后的准星一致性也没有形成独立验收证据。该项作为已知限制保留。

### 2.3 Phase 7F-C：Fab 引用与 HLOD

提交：`558ff39 Phase 7F-C: remove Fab barn reference and rebuild HLOD`

主地图已经移除 Fab `barnhouse` 引用，改用仓库内已跟踪建筑，并重建 HLOD。旧 Fab barn 引用检查为0。用户可以在确认引用清空后自行删除未跟踪 Fab 素材目录。

### 2.4 NavMesh 保存

提交：`0a92ad1 Phase 7F-E: freeze validated NavMesh bounds`

该提交按项目作者批准保留当前NavMesh Bounds。正式有画面运行随后暴露fixed tile pool容量不足。作者于2026-08-31批准修正：RecastNavMesh改为固定tile pool 32768，分区NavData重建成功，0 Error；修正后的World Partition四格运行期tile attach失败为0。该资产修正已随 Phase 7F 封板提交 `3314034` 保存。

### 2.5 Phase 7F-D：正确性关闭

已完成的证据：

- Editor Development 编译成功；
- Game Development 编译成功；
- Phase 7F-A 专项 5/5 Success；
- `AILODResearch` 完整回归 72/72 Success；
- Phase 7 地图 Cook 成功，0 Error；
- Cook 只保留2条 motion-control 相关 Warning；
- 第一次 Cook 遇到 shader transfer 临时文件问题，重试后成功；该失败不是 OOM。

原始日志位于 `Saved/Logs/Phase7F_D_*`。本轮最终采样驱动完成后又执行一次 Phase 7F-A 定向回归，结果仍为 5/5 Success，报告位于 `Saved/Automation/Phase7F_E_PreMatrix/`。

## 3. 7F-E 测试协议

| 项目 | 固定值 |
|---|---|
| 构建 | Unreal Editor Development，`-game` 独立运行 |
| RHI/GPU | DX12 / NVIDIA GeForce RTX 4050 Laptop GPU |
| 驱动 | 556.12 |
| CPU | Intel Core i7-14650HX，16核/24线程 |
| 内存 | 约16 GB |
| 分辨率 | 1920×1080 Windowed |
| 画质 | Epic，Screen Percentage 100 |
| 同步 | VSync Off，帧率不封顶 |
| Seed | 20260810 |
| 调试标签 | On |
| 规模 | 2k、20k |
| 时间倍率 | 1x、4x |
| 每格时长 | Running 后预热15秒，再录制30秒 |
| 重复次数 | 每个配置1次 |
| 场景数 | 6 |
| 总格数 | 24 |

六类场景：

1. `StaticNormal`：固定普通街景；
2. `FastTraversal`：相机以确定性路径约35 m/s绕圈并持续旋转；
3. `DenseProxies`：固定拉高镜头，2k/20k 都稳定显示93个 Proxy；
4. `ActorCap50`：创建50槽 Actor 池，把普通 Active 预算设到允许上限44；
5. `WorldPartitionTravel`：相机和 Pawn 在首尾居民区之间往返，触发真实加载/卸载；
6. `TelescopeLift`：固定周期自动开启望远镜、等待 Streaming Ready、Lift、关闭并清除。

场景驱动只在存在 `-AILODPerfScenario` 命令行参数时启动。普通 PIE、正式固定 Activation Trace 和 Phase 8 Runner 不进入该路径。

## 4. 数据完整性

- `formal1`原矩阵：CSV、UE日志和manifest均为24/24；
- `navfix1`复测：CSV、UE日志和manifest均为4/4；
- 两个run-set合计28次Capture全部为`valid_capture=True`、`scenario_evidence_present=True`、`FullPopulationScan=0`；
- `navfix1`四份日志全部使用RTX 4050 / D3D12 Adapter 0，全部正常结束Capture并记录`Shaders left to compile 0`；
- `navfix1`四份日志的Fatal、Assertion、GPU Crash、项目Log Error和`Failed to add tile`均为0。

这里的 `scenario_evidence_present` 表示场景动作真实发生。ActorCap50 的场景证据定义为“50槽池存在且有完整 Actor 绑定”；是否达到50个同时绑定由单独字段 `actor_cap_reached` 判断，24格该字段均为 False。

两个正式run-set使用同一DLL。`formal1`审计文件记录：

- DLL SHA-256：`19de5407a6e806940d350c661771d0b8c509a1d857b2aca9768186398bf572bd`；
- DLL 最后写入时间早于全部 formal1 Capture；
- 运行相关4个 C++ 文件和 Capture 脚本的 Git blob；
- 24份 CSV、24份日志、24份 manifest 的 SHA-256。

重要口径：`formal1`的24格共享旧NavMesh资产状态；`navfix1`的4格共享修正后的NavMesh资产状态。作者批准的复测范围只有World Partition四格，因此第5节保留其他20格的`formal1`数据，并用`navfix1`替换四个World Partition结果。它适合Phase 7F工程关闭；跨场景内存比较仍需考虑NavMesh资产状态不同，不能当成Phase 8同一构建的正式统计矩阵。

## 5. 24格工程参考结果

`FPS` 为 `1000 / 平均 FrameTime` 的派生值。它方便阅读，P50/P95/P99 才是主要判断依据。

| 场景 | 人口 | 倍率 | FPS | Frame P50 | P95 | P99 | Max | Game P99 | GPU P99 | Proxy均值 | Active均值 |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| StaticNormal | 2k | 1x | 71.8 | 13.93 | 14.54 | 15.09 | 16.12 | 4.30 | 13.54 | 33.0 | 35.0 |
| StaticNormal | 2k | 4x | 71.3 | 14.02 | 14.63 | 15.13 | 15.97 | 6.63 | 13.61 | 33.0 | 35.0 |
| StaticNormal | 20k | 1x | 69.1 | 14.45 | 15.21 | 15.67 | 16.56 | 8.48 | 14.17 | 93.0 | 35.0 |
| StaticNormal | 20k | 4x | 68.9 | 14.51 | 15.20 | 15.70 | 16.70 | 11.06 | 14.28 | 93.0 | 35.0 |
| FastTraversal | 2k | 1x | 67.6 | 14.81 | 16.00 | 18.57 | 20.18 | 8.91 | 14.63 | 18.0 | 32.0 |
| FastTraversal | 2k | 4x | 67.2 | 14.88 | 16.19 | 18.88 | 22.37 | 15.26 | 14.75 | 18.0 | 32.1 |
| FastTraversal | 20k | 1x | 65.6 | 15.22 | 16.96 | 19.18 | 21.23 | 16.93 | 15.54 | 46.6 | 32.4 |
| FastTraversal | 20k | 4x | 65.3 | 15.21 | 18.90 | 22.08 | 32.19 | 22.01 | 15.54 | 46.5 | 32.1 |
| DenseProxies | 2k | 1x | 69.8 | 14.34 | 15.07 | 15.49 | 16.11 | 4.78 | 13.81 | 93.0 | 35.0 |
| DenseProxies | 2k | 4x | 69.5 | 14.40 | 15.05 | 15.41 | 16.22 | 7.25 | 13.86 | 93.0 | 35.0 |
| DenseProxies | 20k | 1x | 68.4 | 14.61 | 15.26 | 15.80 | 16.24 | 8.61 | 14.12 | 93.0 | 35.0 |
| DenseProxies | 20k | 4x | 67.8 | 14.74 | 15.45 | 15.98 | 16.84 | 11.77 | 14.42 | 93.0 | 35.0 |
| ActorCap50 | 2k | 1x | 69.6 | 14.37 | 15.17 | 15.71 | 16.15 | 4.89 | 13.84 | 84.0 | 44.0 |
| ActorCap50 | 2k | 4x | 69.1 | 14.48 | 15.19 | 15.61 | 16.73 | 7.54 | 14.08 | 84.0 | 44.0 |
| ActorCap50 | 20k | 1x | 67.7 | 14.78 | 15.52 | 15.91 | 16.57 | 8.64 | 14.45 | 84.0 | 44.0 |
| ActorCap50 | 20k | 4x | 67.2 | 14.88 | 15.66 | 16.07 | 16.97 | 11.71 | 14.73 | 84.0 | 44.0 |
| TelescopeLift | 2k | 1x | 65.6 | 15.35 | 16.21 | 19.30 | 40.44 | 15.26 | 15.64 | 87.7 | 36.1 |
| TelescopeLift | 2k | 4x | 64.1 | 15.65 | 16.70 | 26.89 | 73.82 | 17.49 | 16.37 | 87.8 | 36.0 |
| TelescopeLift | 20k | 1x | 62.1 | 16.08 | 17.53 | 19.01 | 38.49 | 12.87 | 17.11 | 117.0 | 34.0 |
| TelescopeLift | 20k | 4x | 62.4 | 15.88 | 17.26 | 18.78 | 58.02 | 13.95 | 18.09 | 117.2 | 34.1 |
| WorldPartitionTravel（navfix1） | 2k | 1x | 68.2 | 14.62 | 15.67 | 26.18 | 43.84 | 13.06 | 14.71 | 8.8 | 21.1 |
| WorldPartitionTravel（navfix1） | 2k | 4x | 67.1 | 14.78 | 15.89 | 26.36 | 93.84 | 16.02 | 14.86 | 8.3 | 21.1 |
| WorldPartitionTravel（navfix1） | 20k | 1x | 60.2 | 15.86 | 25.47 | 37.49 | 76.04 | 35.67 | 19.28 | 21.4 | 21.8 |
| WorldPartitionTravel（navfix1） | 20k | 4x | 56.0 | 16.10 | 33.11 | 52.40 | 121.57 | 52.26 | 20.91 | 20.4 | 21.1 |

所有时间单位均为毫秒。

## 6. 大白话解读

### 6.1 普通镜头

普通街景从2k增加到20k后，画面只多显示当前空间查询范围内的居民。20k 静态镜头平均显示93个 Proxy 和35个 Active，系统没有每帧处理全部20k人的画面对象。Frame P99 从2k/1x的15.09 ms增加到20k/1x的15.67 ms。

4x 主要增加 Game Thread 的模拟步进压力。静态20k的 Game P99 从1x的8.48 ms增加到4x的11.06 ms；Frame P99仍接近15.7 ms，因为这组静态镜头的 Render/GPU 时间占主要部分，且还有余量。

### 6.2 快速移动

FastTraversal 四格都记录到约35 m/s的真实镜头移动。20k/4x的 Frame P99达到22.08 ms，Game P99达到22.01 ms，GPU P99为15.54 ms。快速更新可见集合、Actor绑定和模拟步进共同增加了尾部压力。

### 6.3 Proxy 和完整 Actor

DenseProxies 稳定显示93个 Proxy，Frame P99最高15.98 ms。这组没有触达普通 Proxy Budget=128，因此它代表“比2k普通街景更密”的视角，不能代表最大128代理压力。

望远镜会同时保留普通和远距代理。20k望远镜场景的 Proxy 峰值达到160，符合普通代理与 Telescope Proxy 两组预算并存的结构。

ActorCap50 四格都创建50槽 Actor 池并稳定绑定44个完整 Actor。Frame P99最高16.07 ms。隐藏池槽的成本和44个正在绑定、渲染、移动的 Actor 成本不同，当前数据只证明44个实际绑定的表现。

### 6.4 World Partition

WorldPartitionTravel 四格的 Loaded Level 数都发生变化，Streaming Ready→Busy 也多次发生，说明测试确实触发分区加载/卸载。

NavMesh修正后的20k/4x仍是当前最差组合：1680个采样帧中，580帧超过16.67 ms，82帧超过33.33 ms，19帧超过50 ms，1帧超过100 ms。Frame Max为121.57 ms，Game Max为120.86 ms。该尖峰主要落在Game Thread，GPU P99为20.91 ms。

与旧`formal1`相比，20k/4x的Frame P99从58.12 ms降到52.40 ms，最大帧从142.69 ms降到121.57 ms；2k/1x的Frame P99从37.12 ms降到26.18 ms。四份新日志的NavMesh tile attach警告都从数千条降为0。每格只有一次运行，数值差异同时包含正常运行波动，因此只能确认容量告警已经消失，并观察到尾部有所改善，不能把全部改善量都归因给NavMesh。

### 6.5 望远镜

每格完成4次开启、4次 Lift 和3次清除，Streaming Ready至少到达一次。20k查询访问的望远镜居民条目峰值为3695，仍远低于全人口20k，并且 `FullPopulationScan=0`。

望远镜场景会触发远距加载、相机模式切换和 Active 上下文变化。四格的 Frame P99范围为18.78—26.89 ms，最大帧范围为38.49—73.82 ms。

## 7. 内存、显存和 Draw Call

旧`formal1` 24格的进程物理内存峰值范围为`2771.81—2878.25 MB`。NavMesh池扩大后，`navfix1`四格的进程物理内存峰值为`2885.61—2951.22 MB`；显存峰值为`2776.59—2844.97 MB`。固定池扩大与运行缓存都会影响整个进程内存，当前数据没有把两者单独拆开。

这些内存数字包含 Unreal Engine、插件、地图资源、纹理缓存、CSV Profiler、UI和模拟。2k有时高于20k，说明进程缓存波动足以覆盖居民规模差异。论文可以报告“整个 Demo 进程在本机的观测范围”，不能把这些数写成“2k或20k NPC本身占用多少内存”。

Draw Call 同时受到可见建筑、HLOD、World Partition单元、UI、望远镜视角和NPC影响。它适合描述完整场景成本，不适合单独归因给 HISM 或 Actor 数。

## 8. 风险和限制

### 8.1 NavMesh fixed tile pool

旧Recast NavMesh运行期固定池上限为288。22份普通`formal1`日志各有3937条tile attach失败；两份20k World Partition日志各有7875条。20k World Partition的第二批警告出现在正式Capture开始之后。

已执行的修正：

- 保留原NavMesh Bounds和4个分区NavDataChunk；
- RecastNavMesh启用固定池，并把`TilePoolSize`设为32768；
- 选择32768是因为当前4个区块按旧日志推算最多约`4 × 4225 = 16900`个tile，32768是高于该数量的下一档2的幂；
- UE命令行导航重建耗时149.15秒，成功结束，0 Error；
- `navfix1`四份World Partition运行日志的`Failed to add tile`均为0。

剩余边界：测试证明本次往返路线能够装入所需导航tile。它没有逐点执行整张地图的寻路查询，也没有证明所有地面都连续可达。32768会预留更大的tile索引池，整个进程的内存峰值也比旧World Partition四格高约36—96 MB；当前不能把这段差值全部解释为NavMesh实占内存。

### 8.2 50 Actor口径

当前规则的 `NormalActiveActorBudget` 最大为44，`TelescopeActiveBudget` 为5，Active硬上限为50，并保留一个安全余量。当前规划组合的实际峰值为44。

建议把本阶段验收口径写成“50槽固定池，当前冻结策略下最多44个普通完整 Actor 实际绑定”。研究主张依赖的是有界完整 Actor 数和跨LOD连续性，池槽必须恰好全部占满并非核心研究变量。若必须测50个同时绑定，需要先批准改变展示预算组合，再重新评估 Active硬门和望远镜上下文。

### 8.3 统计可信度

- 每个配置只有1次30秒 Run；当前结果属于单机工程证据；
- `navfix1`只按作者批准范围重跑World Partition四格，其他20格仍来自旧NavMesh资产状态；
- 构建为 Development；论文正式性能要求使用冻结后的 Shipping/正式 Runner；
- 标签保持开启，结果代表当前演示默认可读状态；
- CSV自定义统计和自动场景驱动本身有少量开销；
- 没有预先设定60 FPS或其他通过线，本检查点报告分布和风险；
- 7F-B被作者推迟，望远镜遮挡与准星一致性仍是限制；
- 100k未运行；
- Phase 8 的 480 次准确性和 90 次正式性能实验后来已在独立分支完成；该后续结果以 `AILOD_MVP_Phase8_Final_Checkpoint_CN.md` 为准，不回写为 Phase 7F 有画面证据。

## 9. 能证明什么

当前证据可以支持：

- 2k和20k的有画面互动 Demo 能在当前机器、Development、DX12、Epic、1080p下完成30秒采样；
- 普通镜头、快速移动、代理密集、44个完整 Actor绑定、World Partition往返和重复望远镜Lift都能运行；
- 可见查询和展示没有每帧扫描全部人口；
- Proxy/Actor数量受预算约束；
- 4x会增加Game Thread压力；
- World Partition和望远镜会产生可测加载尖峰；
- 修正后的World Partition四格没有再发生NavMesh tile attach容量失败；
- 当前20k/4x World Partition仍存在明显Game Thread长帧。

## 10. 不能证明什么

当前证据不能直接支持：

- 20k在所有镜头和所有机器上都稳定达到60 FPS；
- World Partition尖峰全部来自某一个子系统；
- 整张地图NavMesh完整；
- 50个完整Actor已经同时绑定；
- 100k是默认可演示规模；
- 进程内存等于NPC数据内存；
- HISM、Mass、AnimToTexture中的任一种方案天然最优；
- 仅凭 Phase 7F 证据得出 Proposed 相对 Simple 或 Per-Agent 的论文正式速度优势；
- 仅凭 Development 有画面数据推断 Shipping/NullRHI 算法性能。Phase 8 正式结果必须单独引用 Phase 8 最终检查点。

## 11. 原始证据和复现入口

运行工具：

- `Tools/Phase7F/Run-VisualCapture.ps1`
- `Tools/Phase7F/Analyze-VisualCsv.py`
- `Tools/Phase7F/Configure-Phase7NavMesh.py`

formal1证据：

- 原始CSV：`Saved/Profiling/CSV/Phase7F_E_formal1_*.csv`
- UE日志：`Saved/Logs/Phase7F_E_formal1_*.log`
- manifest：`Saved/Profiling/Phase7F/Manifests/Phase7F_E_formal1_*.json`
- 汇总：`Saved/Profiling/Phase7F/Phase7F_E_formal1_Summary.csv`
- 文件指纹审计：`Saved/Profiling/Phase7F/Phase7F_E_formal1_Audit.json`
- 采样前5项回归：`Saved/Automation/Phase7F_E_PreMatrix/`

NavMesh修正与复测证据：

- 参数写入：`Saved/Logs/Phase7F_E_NavMeshConfigure.log`
- 持久化复核：`Saved/Logs/Phase7F_E_NavMeshFinalInspect.log`
- NavData重建：`Saved/Logs/Phase7F_E_NavMeshRebuild.log`
- `navfix1`原始CSV：`Saved/Profiling/CSV/Phase7F_E_navfix1_*.csv`
- `navfix1` UE日志：`Saved/Logs/Phase7F_E_navfix1_*.log`
- `navfix1` manifest：`Saved/Profiling/Phase7F/Manifests/Phase7F_E_navfix1_*.json`
- `navfix1`汇总：`Saved/Profiling/Phase7F/Phase7F_E_navfix1_Summary.csv`

早期 `Pilot`、`Validation`、没有 `formal1` 前缀的 Capture，以及第一次 `View/Speed=0` 的人工移动样本全部排除在24格正式工程矩阵之外。

## 12. 作者验收与封板

作者已于2026-08-31批准：

1. 保留NavMesh和现有Bounds；
2. 使用“50槽池、44个实际绑定”的口径；
3. 提高Fixed Tile Pool、重建NavData，并只重跑WorldPartitionTravel四格。

上述三项已经完成。作者随后批准 Phase 7F 收口，相关 C++、NavMesh 资产、工具和本文档已以 `3314034` 提交。项目已经在独立的 `phase-8-formal-experiments` 分支完成与互动 Demo 隔离的 Phase 8 正式实验。未经要求仍不 push。
