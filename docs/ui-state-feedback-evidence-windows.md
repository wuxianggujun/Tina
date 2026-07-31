# UI-STATE-FEEDBACK Windows Evidence

This record preserves the reproducible result after generated screenshots and build
artifacts are reclaimed. It is same-host/backend differential evidence, not a
cross-GPU exact golden.

## 2026-07-31

- Source commit: `0cc23e9df7d9b3152872acd413d1ca046b8d6194`
- Configure preset: `windows-msvc-vnext-bgfx-ui-freetype`
- Build preset: `windows-vnext-bgfx-ui-freetype-debug`
- Generator: Visual Studio 18 2026
- Executable SHA-256:
  `3c9bc33b898131f498dbc91697f46881a54c4ec7bd5a4bf17b4a3aecde86b813`
- Gate: `tools/windows/RunUiStateFeedbackVisualGate.ps1`, schema 1
- Result: `ok=true`, 22 checks, 0 failures
- Dark process: 912 frames, exit code 0, `PrimaryWindowRequestedClose`
- Light process: 1053 frames, exit code 0, `PrimaryWindowRequestedClose`
- Dark normal SHA-256:
  `b0eb7a39fc25d5ac95935f1a0eccf016d9ac2ea392561ada433f90d16186ea5e`
- Light normal SHA-256:
  `1749db647e24168ed0159ac47874325d78e4d2a78176a005d6df8793be3062c2`
- `darkLightNormalDifferent=true`
- `darkLightDisabledDifferent=true`

The gate drove pointer input through Win32 into the product GLFW/Runtime/UI route.
Every captured state required two identical useful client-frame fingerprints before
ROI comparison. The first attempt was discarded before evidence publication because
another desktop HWND covered the injected pointer target; the isolated retry above
completed without failures.

The gate verifies Dark/Light normal, hover, focus, pressed/drag, selected, and disabled
state differentiation. Unit and Runtime UI tests remain authoritative for stale-state
cleanup and atomic commit failure paths.
