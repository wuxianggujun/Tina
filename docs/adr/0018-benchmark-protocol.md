# ADR 0018：版本化 benchmark 协议与固定门禁机

- 状态：Accepted（schema v1 首切片；共享机仅 provisional）
- 日期：2026-07-16
- Accepted：2026-07-24（`tina_bench` schema v1 + `null_runtime_frames` workload；
  hard-gate 固定机与多进程 MAD 仍后置）

实现进度（2026-08-01）：`tools/bench/run_benchmark_gate.py` 已提供 schema v1 多进程采集、
run-level p99 median/MAD、workload/fingerprint/checksum 兼容校验与噪声拒绝。仓库仍没有受审
machine profile 和 baseline；因此默认结论继续为 `provisional`，尚未建立 fixed-machine hard gate。

## 背景

平均 FPS、一次本地运行或带 Tracy 的 capture 都不能稳定判定性能回归。不同硬件、编译选项、
worker、驱动和电源状态之间比较绝对时间会产生伪结论。

## 推荐决定

独立 Release `tina_bench` 使用 schema v1、版本化 workload/seed/parameters/checksum 和完整 build/
host fingerprint。每进程 warm-up 600，普通采样至少2,000，正式 p99/泄漏10,000；候选至少5个
独立进程，初建 baseline 10个。Quantile 用 nearest-rank，以 run-level p99 的 median/MAD 比较，
差异同时超过10%和绝对噪声门槛才判相对回归。只有经噪声校准的固定 machine profile 可做
绝对 hard gate；共享 CI/当前开发机只作确定性或 provisional 结论。正式 benchmark 关闭 Tracy。

## 代价

- 需要维护基准机、workload 版本、JSON 比较器和 baseline 审核；
- 正式 p99 门禁耗时比单次微基准长；
- 固定机器尚未选定前，绝对毫秒预算不能声称已通过。

## 被拒绝方案

- 平均 FPS/单次 stopwatch：掩盖长尾且噪声不可知；
- 直接比较不同 fingerprint：不是同一实验；
- Tracy capture 作为 hard baseline：插桩改变程序且 capture 不可重复。
