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

## Narrator / Inspect

Automatic gate **does not** set `narratorGold=true`. Follow
[ui-002-narrator-inspect-checklist.md](ui-002-narrator-inspect-checklist.md) and attach
dated notes before claiming UI-002 full Done.
