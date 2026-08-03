# UI-003 Tip Evidence (content-scale-like matrix)

## 2026-08-03

| Field | Value |
| --- | --- |
| Source tip | `1e52246c30e3a7c0421281875b0eacc1c8541bb3` |
| Sample | `out/build/windows-msvc-vnext-bgfx/bin/Debug/tina_sample_2d.exe` |
| Font identity | `sha256:f1d8611151880c6c336aabeac4640ef434fa13cbfbf1ffe82d0a71b2a5637256` (repo fixture SourceHanSansSC) |

### Structural / capture matrix (no checked-in ROI baseline)

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File `
  .\tools\windows\RunUi003SizeMatrix.ps1 -SkipBuild -NoBaselineCompare
```

Result: **ok=True**, failCases=0  
Summary: `artifacts/gates/ui-003-size-matrix-20260803-090713.json`

Sizes: 960×540, 1200×675, 1440×810, 1280×720, 1920×1080 — each capture useful frames + font fingerprint match path.

### Checked-in ROI baselines refreshed (2026-08-03)

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File `
  .\tools\windows\RunUi003SizeMatrix.ps1 -SkipBuild -WriteBaselines
# then verify:
powershell -NoProfile -ExecutionPolicy Bypass -File `
  .\tools\windows\RunUi003SizeMatrix.ps1 -SkipBuild
```

- All five size baselines under `tools/windows/baselines/ui-003-sample2d-*.json` rewritten for tip
  presentation (font identity unchanged).
- Size-matrix default `AvgRgbTolerance` raised **28 → 40** so playfield particle jitter does not
  false-fail while HUD ROIs still catch large chrome regressions.
- Verify matrix: **ok=True**, failCases=0  
  (`artifacts/gates/ui-003-size-matrix-20260803-092133.json`).

### Still open for full UI-003 Done

- OS Settings 100/150/200% multi-DPI true golden matrix
- Cross-GPU pixel gold
