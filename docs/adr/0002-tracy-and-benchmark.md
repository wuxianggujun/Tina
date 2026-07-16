# ADR 0002：Tracy 用于定位，tina_bench 用于回归

- 状态：Accepted
- 日期：2026-07-16

## 背景

Carbon Core 实际使用 Tracy，而不是 TinyProfile。Profiler 插桩会改变被测程序，不能独自
给出稳定性能门禁；只保留 benchmark 又难以定位回退原因。

## 决定

Tina 提供编译期 `TINA_TRACE_*` 前端和空 backend；Tracy 0.13.1 是首个可选 Profile backend，
只进入开发 Profile 构建。正式 `tina_bench` 默认关闭 Tracy，以版本化 workload、JSON schema、
固定 machine profile 和 p50/p95/p99 负责回归。Metrics 常驻成本和 Tracy 插桩成本分别 A/B。

## 结果

- 业务 API 不暴露 Tracy；
- Profile capture 能解释 phase、线程、锁和内存热点；
- benchmark 必须维护固定机器、统计协议和结果格式；
- Tina backend selection 必须唯一传播；Tracy definitions/header/client 只存在于 adapter target。

## 被拒绝方案

- 只用 Tracy capture 作为 hard baseline：不可重复且有插桩开销；
- 同时接入 MicroProfile：重复事件、线程、包体和维护成本，当前无缺口证据。
