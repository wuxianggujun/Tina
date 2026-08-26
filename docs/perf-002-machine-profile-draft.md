# PERF-002 Machine Profile Draft (dev host)

Template: `tools/bench/profiles/dev-host-candidate.machine-profile.template.json`

## Captured host facts (2026-08-03)

| Field | Value |
| --- | --- |
| machineId (draft) | `dev-host-candidate-not-approved` |
| OS | Microsoft Windows 11 Pro 10.0.26200 |
| CPU | 11th Gen Intel Core i5-11320H @ 3.20GHz (4C/8T) |
| GPU | Intel Iris Xe Graphics, driver 32.0.101.7080 |
| RAM | ≈15.8 GiB (16952647680 bytes) |
| Device | TIMI A29R |
| Power / affinity | unspecified / unpinned (not gate-ready) |

## Why this is only a draft

Hard gate requires **all** of:

1. Clean git + **Release** `tina_bench` (Tracy off)
2. `build.binarySha256` exact match to that executable
3. `benchmarkFingerprint` copied from formal multi-process candidate reports
4. `noiseCalibration` from **≥10** independent processes (observed median/MAD + floors)
5. `review.status = "approved"` with reviewer + date
6. Matching `tina_bench_baseline` on the same `machineId`

Debug provisional multi-process runs already exist (`docs/perf-002-provisional-evidence.md`) and prove
protocol compatibility only (`conclusion=provisional`, `hardGateEligible=false`).

Editor Layout Debugger 的 `--profile-ui-layout-drag` 是独立的开发机 deterministic workload，不属于
PERF-002 hard gate。它固定使用 30 warm-up / 120 mutation / 5 cooldown 帧；结果必须同时保留 JSON
工作量计数和 Tracy phase capture，不能以 Debug 单机 FPS 直接替代 Release、多进程、受审机器 profile。

## Next operator steps

```powershell
# 1) Configure/build Release bench on this host (or the real gate machine).
# 2) Run 10-process formal sample (warmup>=600, samples>=2000) for one frozen workload.
# 3) Fill noiseCalibration + fingerprint + binarySha256 in the template.
# 4) Create matching baseline JSON; set both reviews to approved.
# 5) py -3 tools/bench/run_benchmark_gate.py --hard-gate --machine-profile ... --baseline ...
```
