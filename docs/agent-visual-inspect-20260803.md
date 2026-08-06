# Agent visual inspection (2026-08-03)

Screenshots under `artifacts/screenshots/agent-inspect-20260803/` (gitignore).
Gates re-run the same day: UI-STYLE visual ok; UI-002 UIA tip-r2 ok; UI unit filters 51/51.

## Samples reviewed

### 1. `tina_sample_ui_showcase` (FreeType dark)

Path: `showcase-freetype-dark/.../frame-02.png`

| Check | Result |
| --- | --- |
| Title / subtitle readable (EN + 中文「即时换肤」) | Pass |
| Nav sections (Buttons / Value / Form / Theme / Data) | Pass |
| Primary / Disabled / Reset buttons + checkbox | Pass |
| Sliders + Progress 72% | Pass |
| TextEdit `Tina Player`, radio Performance/Balanced/Quality | Pass |
| Theme Dark/Light + image swatches + status bar | Pass |
| Dropdown + virtual list selection highlight | Pass |
| TreeView hierarchy + settings feed scroll | Pass |
| Layout density / no obvious clip or overlap | Pass |
| Dark theme stylesheet header accent (cyan bar) | Pass |

**Issues:** none blocking. Placeholder-less FreeType build previously showed blank bars only (glyph atlas empty) — expected without FreeType fixture.

### 2. `tina_sample_2d` (FreeType 960×540)

Path: `sample2d-freetype/.../frame-02.png`

| Check | Result |
| --- | --- |
| Title `TileMap 2D` + 中文地图 | Pass |
| Scene Explorer tree expand/selection | Pass |
| Playfield + character/FX visible | Pass |
| Audio panel sliders / mute / progress | Pass |
| Player name TextEdit 中文 | Pass |
| Windowed/Fullscreen radios | Pass |

**Issues:** none blocking. Particle/trail motion is intentional product demo (UI-003 playfield ROI jitter source).

> 2026-08-07：本节保留当时的 target/path 作为历史证据；当前产品入口已迁移为
> `src/editor_app/main.cpp` + `tina_editor_desktop` / `TinaEditor.exe`。

### 3. `tina_sample_editor_shell` (bgfx Debug, historical name)

| Check | Before | After |
| --- | --- | --- |
| Three-column Hierarchy / Viewport / Inspector | Pass | Pass |
| Tree selection + expand rows | Pass | Pass |
| Stylesheet dock/viewport fills | Pass | Pass |
| Center viewport empty black | **Fail (silent void)** | Fixed: multi-line placeholder labels |

**Fix:** `samples/editor_shell/main.cpp` — viewport panel padding + four explicit placeholder labels so screenshots are not a blank elevated panel.

**FreeType recapture** (`editor-shell-freetype/.../frame-02.png`): Hierarchy shows Scene/Camera2D/Player/… selection; Inspector Name/Kind/Note for Scene; center shows four readable placeholder lines (Viewport placeholder / No live Scene bind / stylesheet token / Not 2D-EDITOR). Without FreeType, labels still layout as empty bars only — expected for the plain bgfx Debug graph.

## Automated re-checks (same day)

| Gate / suite | Result |
| --- | --- |
| `RunUiStyleVisualGate.ps1 -SkipBuild` | ok, maxChannelDelta≈31.96 |
| `RunUi002UiaGate.ps1` tip-r2 | ok, providers=69, focusVerified |
| `tina_ui_tests` Motion/Style/Image/Accessib filter | 51/51 |
| editor_shell `--frames=30` smoke | ok (after viewport text fix) |

## Residual product gaps (not screenshot bugs)

- UI-002 Narrator/Inspect still manual
- UI-003 OS multi-DPI / cross-GPU still open
- PERF-002 hard gate / SDK cross-distro still open
- Editor shell still read-only mock (by design)
