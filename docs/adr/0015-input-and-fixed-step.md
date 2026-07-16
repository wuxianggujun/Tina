# ADR 0015：InputFrame、Action domain 与逐 substep 提交

- 状态：Proposed
- 日期：2026-07-16

## 背景

每帧最终按键布尔值无法表达同 Poll 的 Down→Up、Wheel/Text/IME 顺序；UI 之后才路由会产生
玩法穿透；把同一 Press 同时交给 Frame/Fixed Update 会双重执行。多个追赶步只在帧末提交
World command，又会让第 N+1 步看不到第 N 步结果。

## 推荐决定

Platform 生成 InputFrame（最终 Snapshot + 有序 transition）。UI 使用上一帧稳定布局逐
transition 路由并产生 consumption，Gameplay 随后映射；Action 明确属于 Simulation 或 Frame
domain。Simulation edge 在下一个实际 fixed tick 消费一次，0步保留、4步不重复；Frame edge
只在当帧 Frame Update 一次。固定60 Hz、最多4步，每个 substep 独立 jobs/barrier、稳定 command
merge/commit、Transform propagation，Render 使用 previous/current interpolation。

## 代价

- 需要有界 transition batch、resync、tick latch 和回放格式；
- UI 命中使用上一份 committed geometry；State Transition Commit 位于 Frame Update 后，使动态
  root 能在同帧唯一 UI layout 中进入 snapshot、下一帧才交互；
- 每 substep barrier/commit 有固定成本，需要 benchmark 后再并行。

## 被拒绝方案

- 只保留 pressed/released 布尔快照：同帧事件顺序丢失；
- UI/玩法共用 EventBus 广播：消费和所有权不清；
- 所有 fixed substep 完成后只提交一次结构变化：追赶步语义错误。
