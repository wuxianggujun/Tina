# Tina Game Runtime

Tina is a C++23 2D/3D game runtime. The active product path is vNext Desktop plus
`tina_sample_*`. The Legacy `Tina.exe`, side-scroller, and old UI product graph have been retired.

The current retained UI still lives in `include/tina/ui` and `src/ui`. References to the retired
"Legacy UI" never mean that the current `src/ui` module should be removed.

## What exists today

One line per module. For contract detail see [Public API](docs/public-api.md); for module
boundaries see the matching topic doc.

| Module | What is there |
| --- | --- |
| Runtime | `EngineHost` is the sole non-global composition root. `IGameApplication` owns program lifetime, `IGameState` owns frame behavior, plus a fixed-capacity state stack and four-phase policy |
| Core | `Result`/`Status`, MemoryTag/PMR, generation handles, bounded `JsonDocument`/`JsonValue` parsing, `JsonWriter`, a compile-time strippable logging frontend, and an opt-in process-level last-failure report |
| Platform / Input | Tina contracts with a private GLFW adapter: ordered `PlatformFrame`, action domains, an 8-slot pointer table, gamepad registry. Android and HTML5 backends exist |
| Render | Backend-neutral `RenderFrame`/`RenderScene` with a private bgfx backend: Sprite2D, Opaque3D/Transparent3D Cook-Torrance GGX, IBL, CSM, and spot/point shadows |
| Scene | Generation `EntityId`, transform hierarchy, closed typed read views, runtime metadata, 2D/3D extraction, and `CameraFollow2D` |
| Asset | Catalog/cooked assets, AssetId, Handle/Lease, task-backed loading, GPU upload/retirement, incremental cooker, and source import |
| UI | Retained tree, layout, routing, text/glyph rendering, plus Button/Checkbox/Switch/Slider/ProgressBar/RadioButton/TextEdit/NumberField/ColorPicker, Dropdown/Menu/Dialog/Popup/Tooltip/Snackbar, TabView/SplitView/CollapsibleSection, ScrollView, and virtualized ListView/TreeView/VirtualGridView/DataGrid |
| Math | `Tina::Math` is the single definition point for geometry types: header-only, column-major right-handed `Vec`/`Quaternion`/`Mat4`/`Frustum` with 2D/3D queries |
| Gameplay | A timing toolkit over Core+Math only: `Easing` (28 curves), `Scheduler`, `Action`/`ActionRunner`, `Signal<T>` |
| Animation3D | A pose graph **beside** `Animator3D`, not replacing it: `Skeleton3D`/`Pose3D`, `PoseBlend3D`, `ClipSampler3D`, `BlendTree3D`, a state machine with layers/masks and root motion, and two-bone IK |
| Navigation2D | Immutable weighted grids, generation-safe dynamic blockers, deterministic cardinal/diagonal synchronous and incremental A*, and TileMap material-cost conversion |
| Save | `Tina::Save` versioned slot storage: primary+backup copies with digest validation, `SaveSlotHealth` recovery tiers, and a product-owned migration graph (strictly increasing, no downgrade) |
| Audio / Physics2D | Backend-neutral engine with an optional miniaudio adapter; Box/Circle/Capsule/ConvexPolygon/Chain shapes and Distance/Revolute/Prismatic joints with an optional Box2D 3.x adapter |
| Network | Strict numeric IPv4/IPv6 plus owner-thread, fixed-capacity, non-blocking UDP and TCP, HTTP/1.1, RFC 6455 WebSocket, and name resolution over a transport-neutral `IByteStream` seam. The transport layer has no third-party dependency; TLS is an optional mbedTLS adapter (`TINA_BUILD_NETWORK_TLS`, target `tina_network_tls`) |
| Editor | `TinaEditor.exe` (target `tina_editor_desktop`) is a tool tree **above** the engine, gated by `TINA_BUILD_EDITOR`, and is **not part of the Game SDK**: 2D/3D authoring documents, bounded undo, project browser, source import (see [Editor 2D / 3D](docs/editor-2d.md), ADR 0041) |
| Product gates | `tina_sample_2d` covers Catalog/TileMap/Navigation2D/UI/Audio/Physics2D; `tina_sample_3d` covers glTF/Prefab/Scene/Render; `tina_sample_ui_showcase` is a 24-control workbench with live Dark/Light themes |

**Explicitly out of scope** — do not design against these as if they worked: Jolt 3D physics; the GPU
side of the post-process chain (the contract is public, but a non-empty chain fails closed on bgfx and
only the Null backend really consumes it); a runtime owner for 3D authored scenes (2D has
`Scene2DRuntime`, 3D has no equivalent); gameplay scripting (ADR 0045 is Proposed with zero
implementation); reliable-UDP channels, netcode, NAT traversal, HTTP/2, HTTP/3, DNS caching, proxies,
and certificate pinning (see [network](docs/network.md)).

Public headers and the Game SDK do not expose bgfx, GLFW, Box2D, miniaudio, FreeType, cgltf,
stb_image, MikkTSpace, or xxHash types.

## Windows quick start

Requirements: CMake 3.25+, Visual Studio 2026/MSVC 19.50, and `VCPKG_ROOT`.

```powershell
cmake --preset windows-msvc-vnext
cmake --build --preset windows-vnext-debug --target tina_tests tina_ui_tests tina_runtime_ui_tests tina_sample_null --parallel 2 -- /nr:false
out\build\windows-msvc-vnext\bin\Debug\tina_tests.exe --gtest_color=yes
out\build\windows-msvc-vnext\bin\Debug\tina_ui_tests.exe --gtest_color=yes
out\build\windows-msvc-vnext\bin\Debug\tina_runtime_ui_tests.exe --gtest_color=yes
out\build\windows-msvc-vnext\bin\Debug\tina_sample_null.exe --frames=300
```

```powershell
cmake --preset windows-msvc-vnext-bgfx
cmake --build --preset windows-vnext-bgfx-debug --target tina_sample_2d tina_sample_3d --parallel 2 -- /nr:false
out\build\windows-msvc-vnext-bgfx\bin\Debug\tina_sample_2d.exe --frames=300 --frame-delay-ms=0
out\build\windows-msvc-vnext-bgfx\bin\Debug\tina_sample_3d.exe --frames=30 --frame-delay-ms=0
```

Use the FreeType graph for the complete UI control and theme showcase:

```powershell
cmake --preset windows-msvc-vnext-bgfx-ui-freetype
cmake --build --preset windows-vnext-bgfx-ui-freetype-debug --target tina_sample_ui_showcase --parallel 2 -- /nr:false
out\build\windows-msvc-vnext-bgfx-ui-freetype\bin\Debug\tina_sample_ui_showcase.exe --frames=150 --frame-delay-ms=0 --theme=dark --auto-demo
```

Run GoogleTest executables directly; the project intentionally does not use CTest. Routine gates
must not use `--clean-first` or wipe `out/build`.

Two notes on GPU resource lifetime, since they are easy to conflate: Texture/Mesh/EnvironmentMap
retirement uses backend-proven readback markers, while a *general* GPU submission fence is not part of
the current contract. Cross-GPU visual golden images are still open. See
[testing](docs/testing.md) for what each gate actually asserts.

See the [Chinese project guide](README_CN.md), [documentation index](docs/README.md),
[roadmap](docs/roadmap.md), and [actionable backlog](docs/backlog.md).
