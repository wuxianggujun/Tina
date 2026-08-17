# ADR 0026：UI Keyframe Timeline 与 Layout Animation 边界

- 状态：Accepted
- 日期：2026-08-16
- 决策者：Tina maintainers

## 背景

ADR 0023 与 `UI-MOTION-001` 已落地每窗口 monotonic clock、fixed-capacity paint-only transition、
retarget、reduced-motion，以及 Style `BackgroundColor` 的 persistent reservation/activation。现有 track 只表达
单段 `start -> target`，不能表达多个属性共享时轴的多 keyframe 动画；布局属性仍明确不在可动画集合中。

直接把 keyframe 塞进现有 transition slot、每帧遍历所有 retained node，或在 Runtime 之外增加 animation update
loop，会破坏已冻结的 active-set 成本、唯一 UI commit phase 和失败原子性。Layout animation 还会同时改变
Measure/Arrange、Hit 与 Paint；如果只发布其中一部分，pointer route 会命中与画面不一致的 rect。

本 ADR 先冻结 timeline 与 layout transaction 边界。接受后发布 `UI-MOTION-002` 公共 API；首个实现切片只包含
paint-only timeline，layout property 集另行排片。

## 决定

### 1. 唯一每窗口时轴

1. Keyframe timeline 归每个 `UIContext` 所有，复用 Runtime 已传入 UI coordinator 的
   `Core::MonotonicTimePoint`；不增加线程、timer、全局 scheduler、第二套 game/UI update loop 或 delta 累加时钟。
2. Timeline 以 generation-safe ID 标识。一个 retained timeline definition 同时最多有一个 active playback；
   再次 play 是 retarget，不创建隐式 clone。并发实例化若以后出现，必须新增独立决定和容量单位。
3. 一条 timeline 包含 1..N 条 typed track；每条 track 绑定唯一 `(UINodeId, property)`，同一 timeline 内不得
   重复。Keyframe time 使用归一化 `[0,1]`，严格递增，首帧为0、末帧为1；每条 track 至少两个 keyframe，
   value kind 与 property 必须一致。首版 playback 仅 `Once`，不承诺 pause/seek/repeat/yoyo/completion callback。
4. 同一 `(node, property)` 同时只有一个 motion presentation owner。Active timeline 与直接 transition、Style
   persistent transition reservation 冲突时 fail closed；调用方必须先显式 cancel/解绑，不能靠隐式优先级或
   track-level 抢占造成多 track timeline 失同步。

### 2. 固定容量单位

以下容量在 `UIContext::Create` 时归一化、校验并一次性预留，运行期不得增长或 heap fallback：

| 容量 | 精确计数单位 | 不计入 |
| --- | --- | --- |
| timeline capacity | retained timeline definition record | active playback、track、keyframe |
| timeline track capacity | 所有 definition 拥有的 typed `(node, property)` track record 总数 | 现有单段 transition track |
| keyframe capacity | 所有 track 拥有的 encoded typed keyframe 总数 | 运行期临时插值值 |
| active timeline index capacity | compact active-index 中的 playback entry；每个 active timeline 恰好一个 | timeline 内 track 数 |

统计至少公开四类 capacity/current/high-water、last sampled timeline/track/keyframe-segment count、cancel/retarget
count 与 reduced-motion 状态。active sample 成本按 active timeline 的实际 track/keyframe segment 计，不得用
definition 总数或 node 总数掩盖扫描。

### 3. Authoring、play 与失败原子性

1. create/replace definition 先校验所有 ID、property/value kind、keyframe 顺序、duration/delay、容量与重复绑定，
   再一次发布完整 definition。失败保留旧 definition、active playback、presentation 与 committed snapshot。
2. play 在修改 target、取消旧 presentation 或写 active index 前完成 definition、node generation、property
   capability、presentation-owner conflict 与 active-index 容量预检。任一失败为零发布。
3. Retarget 先在同一 `now` 采样当前 presentation，再以该值替换新 schedule 的 effective keyframe-0 并原子重启；
   不能跳到 authored keyframe-0，也不能先取消旧 playback 后因新 definition/capacity 失败而丢动画。
4. Timeline 开始时 final keyframe 是该 property 的 target；中间 keyframe 只影响 presentation。正常完成或显式
   cancel 都释放 presentation owner 并落到 final target。首版不提供“冻结当前中间值”隐式 authoring。
5. 销毁任一绑定 node 会在 node retirement transaction 中取消整个 active timeline；存活 track 落 final target，
   被销毁 node 不再发布 paint/hit/semantics。销毁 definition 同样先取消 playback，再释放 track/keyframe/index。
   stale generation command 返回显式 error，不作用到复用 slot 的新 node/timeline。
6. `reduced-motion=true` 时 play/retarget 在同一 transaction 直接落所有 final target，不写 active index；运行中切换
   reduced-motion 会在一个 UI mutation 中 snap 全部 active timeline 并清空 active index。

### 4. 首切片只做 paint-only（已接受）

ADR Accepted 后的首个 `UI-MOTION-002` 垂直切片只允许现有
`UIAnimatableProperty` 中的 paint-only color/scalar/offset 类型。Timeline sample 只标记 Paint dirty，不改变
layout rect、hit snapshot、semantics、focus、pointer capture、callback 时序或 node lifetime。它复用现有
committed paint publication 和 fakeable monotonic clock；不得建立第二份 paint tree或 backend handle cache。

首切片同时新增 `ui_motion_timeline_v1`：4096 retained nodes，seed 映射为 0/64/1024 active tracks，active
timeline 每条固定4个 track、每 track 4个 keyframe，并使用固定 fake clock。workload 必须报告 timeline/track/
keyframe/active-index high-water、sampled track/segment、Paint publication 与 checksum；warmup 后 allocation
delta 必须为0，`active=0` 的 clean sample 必须保持 Layout/Hit/Paint rebuild=0，active paint-only sample 的
Layout/Hit rebuild 必须为0。绝对耗时在 `PERF-002` approved baseline 前仍只报 provisional。

### 5. Layout animation 的事务边界

Layout property 集不进入首切片。后续 layout slice 必须同时满足：

1. 每个 monotonic sample 先生成完整 layout-property candidate，再一次标记精确 Measure/Arrange dirty；随后沿唯一
   UI commit pipeline 构建同一时刻的 Layout、Hit、Paint candidate。只有三者全部成功才替换 committed snapshot。
2. capacity、layout、dirty queue 或 snapshot publication 任一失败时，旧 committed Layout/Hit/Paint 和旧
   presentation state 全部保留，不允许画面移动但 hit rect 留在旧位置，也不允许只提交部分 track。
3. 失败不回拨 monotonic clock；下一帧按绝对 `now - startTime` 重新采样候选，因此不会通过重复 delta 累加产生
   时间漂移。持续失败保持最后成功 snapshot，并用结构化 counter/error 记录。
4. Layout/Hit/Paint 使用同一 interpolated geometry；不得用“final hit rect + animated paint transform”冒充 layout
   animation。Focus/capture 仍绑定 generation-safe node，callback 不因动画延期。
5. reduced-motion 只应用 final layout target并执行一次普通原子 commit。Layout workload、允许的 property 白名单与
   每帧 rebuild 预算必须在该 slice 中单独版本化，不能复用 paint-only workload 宣称完成。

## 结果

- Keyframe timeline 仍处于唯一 `UIContext` owner、唯一 UI phase 与 backend-neutral committed snapshot 内；
- timeline/track/keyframe/active-index 的内存和 sample 成本可配置、可统计、可压测；
- retarget、cancel、destroy、reduced-motion 与 presentation conflict 有确定语义，不依赖调用顺序偶然性；
- 首切片能独立交付 paint-only timeline，不把高风险 Layout/Hit 事务混入同一变更；
- 代价是首版没有 loop/seek/pause/clone/completion callback，且 active Style transition 与 timeline 不能隐式叠加；
- 本 ADR 已 Accepted；`UI-MOTION-002` 的首个 paint-only API、固定容量 storage、Runtime facade 与
  `ui_motion_timeline_v1` benchmark 按本决定落地。

## 后续实施记录（2026-08-16）

第5节定义的 layout slice 已在源码中实现 `LayoutWidth`、`LayoutHeight` 与 `LayoutOffset` 白名单。每次
monotonic sample 会同时暂存 layout timeline、同 timeline 的 paint track 以及同帧 direct transition
presentation；只有 Layout、Hit、Paint、Semantics 的全部 candidate builder 成功后，才提交 presentation、
completion target、active playback 与对应 committed snapshot。任一 dirty queue、capacity、layout 或 snapshot
失败都会丢弃本次 candidate，保留最后成功 presentation、retained target 与 active owner；下一次按绝对
`now - startTime` 重新采样，不累加失败帧 delta。`reduced-motion` 先 snap final target 并清空 active index，
再由下一次普通 `commitLayout()` 原子发布最终几何。

源码同时加入 `ui_motion_layout_v1`，以 seed 0/1/2 覆盖 compact active timeline/track=`0/0`、`16/64`、
`256/1024`，并验证固定 definition high-water、Layout/Hit/Paint publication、allocation delta 与 checksum。
本记录不改写 Accepted 时的决策背景。2026-08-16 统一 gate 已通过：`UIMotionTests.*` 28/28、Runtime
`*Timeline*` facade 1/1、`tina_bench_tests` 10/10；paint/layout workload 均以 `warmup=30,samples=120`
跑通 seed 0/1/2，`layout_commit_failures=0`、UI PMR allocation delta=0。共享开发机的墙钟结论仍为
`provisional`，绝对性能 hard gate 继续由 `PERF-002` 跟踪。

## 被拒绝方案

- 每个 Element 自带 heap timeline/vector：容量与 teardown 不再由 Context 统一证明；
- 每帧扫描整棵树寻找 active animation：成本随无关 node 增长，违反 active-index 模型；
- 在 Runtime/UI commit 之外启动 timer/thread：形成第二套 update loop 和不可复现时钟；
- timeline 与 Style/direct transition 用隐式优先级混合：同一 property 出现两个 presentation owner，retarget/cancel
  无法保持事务性；
- 先发布 paint transform、Hit 直接跳 final rect：交互位置与可见位置不一致，不属于 layout animation；
- snapshot capacity 失败时逐 track/逐 subsystem 提交：会产生半动画和 Layout/Hit/Paint 撕裂；
- 首切片同时实现 loop、seek、callback 与 layout property：扩大 API 和状态机，无法用一个垂直门禁证明正确性。
