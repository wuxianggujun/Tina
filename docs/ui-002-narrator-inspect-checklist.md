# UI-002 Narrator / Inspect Manual Gold Checklist

Operator-only product evidence. Completing this checklist is required to set
`narratorGold=true` / close UI-002 Manual acceptance. The automated
`RunUi002UiaGate.ps1` path is a **prerequisite**, not a substitute.

## Prerequisites

1. Pass tip automatic gate (see [ui-002-uia-evidence-windows.md](ui-002-uia-evidence-windows.md)).
2. Build FreeType + UIA graph:

```powershell
cmake --build --preset windows-vnext-bgfx-ui-freetype-debug `
  --target tina_sample_ui_showcase --parallel 2 -- /nr:false
out\build\windows-msvc-vnext-bgfx-ui-freetype\bin\Debug\tina_sample_ui_showcase.exe `
  --theme=dark
```

3. Tools: Windows **Narrator** (Win+Ctrl+Enter) and/or **Inspect.exe** (Windows SDK).

## Record header (fill every run)

| Field | Value |
| --- | --- |
| Date (local) | |
| Operator | |
| Source commit | |
| OS build | |
| Showcase SHA-256 | |
| Inspect / Narrator version | |
| Pass? (Y/N) | |

## Inspect.exe checks

| # | Check | Pass |
| --- | --- | --- |
| I1 | Tree shows `Tina UI Root` under the showcase HWND | |
| I2 | FrameworkId / provider identity is Tina (not only default HWND proxy) | |
| I3 | Button `Primary action` exposes Invoke pattern | |
| I4 | Checkbox `Enable notifications` exposes Toggle | |
| I5 | Slider exposes RangeValue; value readable | |
| I6 | TextEdit `Profile name` exposes Value; name readable (CJK/Latin OK with FreeType) | |
| I7 | AutomationId values are stable `tina-ui-node-*` for non-root nodes | |
| I8 | After destroy/close, Inspect does not keep live Tina fragments for the closed HWND | |

## Narrator checks

| # | Check | Pass |
| --- | --- | --- |
| N1 | Moving to Primary action announces a useful name (not empty / “unknown”) | |
| N2 | Invoke (Narrator primary action) activates the button; status/name feedback sensible | |
| N3 | Checkbox toggle announces state change | |
| N4 | Slider adjust announces range/value change | |
| N5 | TextEdit reads current value; typing or Set value is reflected | |
| N6 | Focus moves among focusable controls without trapping outside the window | |
| N7 | Closing the window ends narration of that tree (no stale ghost UI) | |

## Notes / failures

```
(free text)
```

## After pass

1. Attach this filled table (or a dated copy under `docs/` / `artifacts/gates/`).
2. Update [ui-002-uia-evidence-windows.md](ui-002-uia-evidence-windows.md) with a Manual section
   and set backlog UI-002 to Done only when both automatic tip gate and this checklist pass.
