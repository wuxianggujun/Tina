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

### Checked-in ROI baseline compare (same tip)

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File `
  .\tools\windows\RunUi003SizeMatrix.ps1 -SkipBuild
```

Result: **ok=False** on all 5 sizes — ROI avgRgb deltas exceed tol=28 vs frozen baselines
(title_plate / scene_explorer / progress_bar / playfield). Font fingerprint still matched.

Interpretation: tip product-2d presentation drifted from committed baselines (theme/UI chrome/content).
Refreshing baselines requires explicit `-WriteBaselines` + human review; not done automatically this run.
OS 100/150/200% DPI and cross-GPU goldens remain open.

### Still open for UI-003 Done

- Decide: refresh ROI baselines under review, or fix product presentation to match gold
- OS-level multi-DPI matrix
- Cross-GPU pixel gold
