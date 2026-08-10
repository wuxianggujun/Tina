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

### 2026-08-03 时点仍开放

- OS Settings 100/150/200% multi-DPI true golden matrix
- Cross-GPU pixel gold

## 2026-08-10 150% / 200% Raster Evidence

当前 Windows 宿主在用户分别切换 OS 显示缩放后完成两组取证：150% 报告
`contentScale=1.5×1.5`、`logicalPixel=960×540`、`framebuffer/capture=1440×810`；200% 报告
`contentScale=2×2`、`logicalPixel=960×540`、`framebuffer/capture=1920×1080`。150% 运行另以
`PER_MONITOR_AWARE_V2` Win32 探针确认 `GetDpiForSystem=144`。
`RunUi003VisualGate.ps1` 现按实测 logical-to-capture scale 映射 ROI，并按 raster scale 选择 sibling baseline；
baseline rect 比较会先还原到 logical space，容许 1 logical pixel 的采样取整差。

- Baseline：`tools/windows/baselines/ui-003-sample2d-960x540-raster-150pct.json` 与
  `tools/windows/baselines/ui-003-sample2d-960x540-raster-200pct.json`
- 150% 独立复跑：`artifacts/screenshots/ui-003-line-ellipse-dpi150-verify/20260810-191624/20260810-191626/ui-003-gate.json`
- 200% 独立复跑：`artifacts/screenshots/ui-003-line-ellipse-dpi200-verify/20260810-175302/20260810-175306/ui-003-gate.json`
- 结果：`ok=true`、`captureOk=true`、`captureLogicalToPixelScale=[2,2]`、
  `baselineCompare.matched=true`、`roiCompared=true`、`errors=[]`；150% 对应 scale 为 `[1.5,1.5]`
- Editor 2D/3D 150% 截图：
  `artifacts/screenshots/editor-ring-150pct/2d-ring-scan/20260810-192248/frame-10.png` 与
  `artifacts/screenshots/editor-ring-150pct/3d/20260810-191806/frame-06.png`；人工检查 grid、斜向 gizmo 与
  Ellipse rotation ring 连续，ring 中心无异常 coverage 点
- Editor 2D/3D 200% 截图：`artifacts/screenshots/editor-ring-200pct/2d/20260810-165944/` 与
  `artifacts/screenshots/editor-ring-200pct/3d/20260810-165624/`；grid、斜向 gizmo 与单 Ellipse rotation ring
  人工检查连续无阶梯。

仍开放：100% OS DPI 金标与跨 GPU 像素金标；因此 UI-003 和 `RENDER-LINES-001` 保持 InProgress。
