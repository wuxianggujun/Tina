# Tina Game Runtime

Tina is a C++23 2D/3D game runtime. The active product path is vNext Desktop plus
`tina_sample_*`. The Legacy `Tina.exe`, side-scroller, and old UI product graph have been retired.

The current retained UI still lives in `include/tina/ui` and `src/ui`. References to the retired
"Legacy UI" never mean that the current `src/ui` module should be removed.

## What exists today

- `EngineHost` is the sole non-global composition root.
- `IGameApplication` owns program startup/shutdown; `IGameState` owns frame behavior.
- Platform/Input uses Tina contracts with a private GLFW adapter.
- Render uses backend-neutral frame/scene data with a private bgfx backend.
- Catalog/Cooked assets, AssetId, Handle/Lease, task-backed loading, GPU upload, and retirement exist.
- The retained UI includes layout, routing, text/glyph rendering, Button, Checkbox, Slider,
  ProgressBar, RadioButton, and single-line TextEdit.
- Audio and Physics2D have optional miniaudio and Box2D adapters.
- `tina_sample_2d` and `tina_sample_3d` are the product smoke entry points.

Public headers and the Game SDK do not expose bgfx, GLFW, Box2D, miniaudio, FreeType, cgltf,
stb_image, or xxHash types.

## Windows quick start

Requirements: CMake 3.25+, Visual Studio 2026/MSVC 19.50, and `VCPKG_ROOT`.

```powershell
cmake --preset windows-msvc-vnext
cmake --build --preset windows-vnext-debug --target tina_tests tina_ui_tests tina_runtime_ui_tests tina_sample_null -- /m:2 /v:m
out\build\windows-msvc-vnext\bin\Debug\tina_tests.exe --gtest_color=yes
out\build\windows-msvc-vnext\bin\Debug\tina_ui_tests.exe --gtest_color=yes
out\build\windows-msvc-vnext\bin\Debug\tina_runtime_ui_tests.exe --gtest_color=yes
out\build\windows-msvc-vnext\bin\Debug\tina_sample_null.exe --frames=300
```

```powershell
cmake --preset windows-msvc-vnext-bgfx
cmake --build --preset windows-vnext-bgfx-debug --target tina_sample_2d tina_sample_3d -- /m:2 /v:m
out\build\windows-msvc-vnext-bgfx\bin\Debug\tina_sample_2d.exe --frames=300 --frame-delay-ms=0
out\build\windows-msvc-vnext-bgfx\bin\Debug\tina_sample_3d.exe --frames=30 --frame-delay-ms=0
```

Run GoogleTest executables directly; the project intentionally does not use CTest. Routine gates
must not use `--clean-first` or wipe `out/build`.

The multi-mesh glTF cooker, per-AssetId resolver coverage, URI/size hardening, and PNG/JPEG
base-color texture cooking are implemented. `tina_sample_3d` maps multiple product meshes and
materials to backend keys; cooked base-color, metallic-roughness, and normal textures are
uploaded, bound, and sampled by the experimental metallic-roughness path with material factors
and key/fill directional lights. Full PBR, IBL, shadows, a general light/pass system, and true
GPU-fence pinning remain open. The accepted task-system policy is implemented: interactive Desktop
defaults CPU workers to `max(1, hw-1)` while explicit settings remain preserved.

See the [Chinese project guide](README_CN.md), [documentation index](docs/README.md),
[roadmap](docs/roadmap.md), and [actionable backlog](docs/backlog.md).
