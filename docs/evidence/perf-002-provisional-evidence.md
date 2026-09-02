# PERF-002 Provisional Multi-Process Evidence (tip)

`tina_bench_multi_process` schema v1 on this developer machine.
**conclusion=provisional**, **hardGateEligible=false** — not an approved fixed-machine hard gate.

## 2026-08-03

| Field | Value |
| --- | --- |
| Source tip | `1e52246c30e3a7c0421281875b0eacc1c8541bb3` |
| Executable | `out/build/windows-msvc-vnext-bgfx/bin/Debug/tina_bench.exe` |
| Runner | `tools/bench/run_benchmark_gate.py` |
| Processes | 5 (sequential independent) |

### Unit protocol

```powershell
py -3 -m unittest tools.bench.test_run_benchmark_gate -v
# 13 tests OK
```

### Multi-process provisional runs (status=ok)

| Workload | Warmup/Samples | Output JSON |
| --- | --- | --- |
| `null_runtime_frames` | 60 / 600 | `artifacts/gates/perf-002-multi-process-null-provisional-20260803.json` |
| `ui_static_commit_v1` | 30 / 120 | `artifacts/gates/perf-002-multi-process-ui-static-provisional-20260803.json` |
| `ui_motion_v1` (seed=0 → M=0) | 20 / 60 | `artifacts/gates/perf-002-multi-process-ui-motion-provisional-20260803.json` |

Example command:

```powershell
py -3 tools\bench\run_benchmark_gate.py --processes 5 `
  --output artifacts\gates\perf-002-multi-process-null-provisional-20260803.json `
  out\build\windows-msvc-vnext-bgfx\bin\Debug\tina_bench.exe -- `
  --workload=null_runtime_frames --warmup=60 --samples=600 --seed=1
```

### Proven

- Independent process collection
- Workload / fingerprint / checksum compatibility across processes
- Run-level p99 median/MAD
- Default mode remains provisional without approved machine profile

### Still open for PERF-002 Done

- Select fixed gate machine (dev host draft facts: [perf-002-machine-profile-draft.md](../perf-002-machine-profile-draft.md))
- Fill `tools/bench/profiles/dev-host-candidate.machine-profile.template.json` from **Release** formal samples
- Commit `status=approved` machine profile + baseline (schema v1)
- Release build, ≥600 warmup / ≥2000 measure, ≥5–10 processes, `--hard-gate`

**Note:** Docker Desktop Linux engine was unavailable on 2026-08-03 tip, so SDK cross-distro was not re-run this session.
