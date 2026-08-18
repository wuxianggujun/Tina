# UI-002 Windows UIA Evidence

Same-host Windows product evidence for the UI-002 external HWND client gate.
This is **not** Narrator/Inspect compliance gold and **not** Linux AT-SPI.

## 2026-08-03 tip automatic gate

| Field | Value |
| --- | --- |
| Source commit | `4da7bc03ee5bbeb32a2ae2b1d4ff391b6bb7614b` |
| Configure preset | `windows-msvc-vnext-bgfx-ui-freetype` |
| Build preset | `windows-vnext-bgfx-ui-freetype-debug` |
| Gate script | `tools/windows/RunUi002UiaGate.ps1` (schema 1) |
| Report JSON | `artifacts/gates/ui-002-uia-20260803-tip.json` |
| Run dir | `artifacts/gates/ui-002-uia-run-20260803-*` |
| Showcase SHA-256 | `6B53DEBA8FC126BD26FACC0B916A3DE0D4D067916257841FE9E5CA762E31726E` |
| `tina_ui_uia_tests` SHA-256 | `593A7C74159C33E9D9D9E5A0FE07A342CDBA45BB4031B51DD4D90454231ED671` |

### Command

```powershell
cmake --build --preset windows-vnext-bgfx-ui-freetype-debug `
  --target tina_ui_uia_tests tina_sample_ui_showcase --parallel 2 -- /nr:false
powershell -NoProfile -ExecutionPolicy Bypass -File .\tools\windows\RunUi002UiaGate.ps1 `
  -SkipConfigure -SkipBuild -OutJson artifacts\gates\ui-002-uia-20260803-tip.json
```

### Results (`ok=true`)

| Check | Result |
| --- | --- |
| `tina_ui_uia_tests` | 12/12 |
| External client | `System.Windows.Automation` |
| Window title | `Tina UI Showcase - Complete Retained Controls` |
| Tina providers | 69 |
| Focusable | 26 |
| FrameworkId `Tina` | 69 |
| Unique RuntimeId | 69/69 |
| Unique AutomationId | 68 (root + 68 nodes) |
| Fragment orphans | 0 |
| Fragment integrity | true |
| Invoke / Toggle / RangeValue / Value | issued + verified |
| Focus (`HasKeyboardFocus` on TextEdit after SetFocus) | verified |
| Primary action Name republish | observed |
| Post TextEdit value | `UIA Player` |
| Post Slider value | 64 |
| Post Checkbox toggle | Off (0) |
| `WM_CLOSE` normal shutdown | exit 0 |
| `narratorGold` | **false** (manual still required) |

### Related unit / integration re-run (same day)

| Suite | Filter / scope | Result |
| --- | --- | --- |
| `tina_ui_uia_tests` | full | 12/12 |
| `tina_ui_tests` | `*Accessib*` | 8/8 |
| `tina_runtime_ui_tests` | PrimaryWindow UI owner/display (prior run 68 matched) | pass |

### Gate hardening (same tip series)

`RunUi002UiaGate.ps1` Focus verification now treats **TextEdit `HasKeyboardFocus` after
`SetFocus`** as the product Focus evidence. Global `AutomationElement.FocusedElement`
AutomationId is diagnostic only: some hosts return an empty id on the HWND root even
when the Tina node reports keyboard focus. Mutations re-assert TextEdit focus each poll
so Toggle/Range/Value do not permanently steal focus before verification.

## 2026-08-18 current working tree automatic gate

The showcase now contains both the single-line profile editor and a multiline
notes editor. Both controls publish explicit accessible names. The external
gate selects the actionable profile editor by `ControlType=Edit` and
`Name=Profile name`, instead of depending on fragment traversal order.

| Field | Value |
| --- | --- |
| Source base commit | `31f7ce68a37420ab942ea6ac92a273fa1571514f` plus the working-tree gate fix |
| Report JSON | `artifacts/gates/ui-002-uia-20260818-200137.json` |
| Showcase SHA-256 | `FEEEC890C8C268B5BDA6B9F8AFE362856E8A60E5957ED8BFD883740E661ADF10` |
| `tina_ui_uia_tests` SHA-256 | `0ECCD1C1A0510CAB7028F7D176EBF87A1E494154572B4B6B2AA6F0069DA21DA3` |

The full gate returned `ok=true`: `tina_ui_uia_tests` passed 12/12, the
external client discovered 71 Tina providers with 71 positive bounds and 27
focusable nodes, RuntimeId/AutomationId uniqueness and fragment integrity
passed with zero orphans, and Invoke/Toggle/RangeValue/Value/Focus mutations
were observed. The profile value changed from `Tina Player` to `UIA Player`,
the slider changed from 72 to 64, the checkbox changed from On to Off, and
`WM_CLOSE` produced a normal exit with no forced termination.

This run still records `narratorGold=false`.

## Narrator / Inspect

Automatic gate **does not** set `narratorGold=true`. Follow
[ui-002-narrator-inspect-checklist.md](ui-002-narrator-inspect-checklist.md) and attach
dated notes before claiming UI-002 full Done.
