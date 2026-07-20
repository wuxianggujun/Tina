# Tina Engine

Tina targets C++23 as its 2D/3D game-runtime language baseline and uses GLFW, a private bgfx backend,
miniaudio, EnTT, and a custom retained-mode UI. The vNext game-facing contracts expose no third-party
or bgfx types; the still-running Legacy implementation has not completed that boundary migration.
The current code keeps verified 2D/UI/3D paths alive while the vNext
architecture is designed as an incompatible, performance-first modular runtime. Migration will use
independently runnable vertical slices rather than an untestable single-shot rewrite.

The public lifecycle names are explicit: `IGameApplication` is the whole program entry point, while
`IGameState` represents a frame-driven menu, level, pause screen, or similar runtime state. The
ambiguous `IGame` name is not part of the vNext API.

Profiling is designed around Tina-owned trace points with optional Tracy. A separate `tina_bench`
performance-regression executable is planned but not implemented yet. SDL/SDL3 and CTest are not part
of the target architecture.

The C++23 headless lifecycle kernel, M7-A platform/input kernel, the first desktop adapter slice,
M7-B1 private WindowSurface handoff, M7-B2 Desktop bootstrap plus real-GPU smoke, and the
M7-C1b/M7-C1c-a/C1c-b1/C1c-b2/C1c-b3a/C1c-b3b/C1c-b3c/C1c-b3d1/b3d2/b3e retained tree, Flex-lite layout,
committed hit-snapshot, point-query, synthetic routed-pointer, private Runtime input-route,
startup primary-window UI seed, scoped Game SDK UI capability, held Pointer-button claim, and the
root-scoped Game SDK routed-pointer listener extension are complete. The private `tina_platform_glfw` backend now
creates a `GLFW_NO_API` window, normalizes keyboard/pointer/focus/resize/close/committed UTF-8 text
into the same bounded `PlatformFrameView`, and hands a move-only window surface lease to the render
composition without exposing native or bgfx types. `Tina::Desktop::CreateEngine(config)` now privately
composes `SteadyClock + GLFW WindowSurface + DisabledTaskSystem + bgfx`, and `tina_sample_desktop`
defaults to 300 frames on the real render-backend path. The latest recorded Windows Debug gate on
VS 2026/MSVC 19.50/CMake 4.2.3 directly passes the vNext Core/Runtime/UI/Scene executables; the current
M8-B validation additionally covers `tina_render_scene_tests` and `tina_sample_2d_infrastructure`.
M8-A covers the fixed-capacity Scene World, generation EntityId, transactional Local/World Transform
publication, keep-world/keep-local hierarchy edits, parent/subtree destruction, overflow/shear diagnostics,
and linear wide-tree cleanup. M8-B adds a fixed-capacity backend-neutral `RenderSceneBuilder`, phase-local
`RenderSceneWriter`, resolved Camera2D/Sprite2D inputs, deterministic layer/order sorting, conservative culling,
pixel snap, Runtime `RenderFrame` handoff, and a headless/Null 2D infrastructure sample. M9-A extends the same
builder with a backend-neutral Perspective Camera and Mesh3D extraction: right-handed Y-up `-Z` forward poses,
positive-scale world bounds, sphere frustum culling, deterministic material/mesh/depth ordering, and adjacent
instance-batch finalization. `tina_sample_3d_extraction` exercises that CPU/Headless/Null boundary only; it is
not visible GPU 3D. M9-B adds the private bgfx Opaque3D fixture path: only
`meshKey=1/materialKey=1/submeshIndex=0` maps to a canonical `P3_N3_UV2` procedural cube, an unlit embedded
shader, depth-tested Opaque3D, and a real bgfx transient instance buffer. The current M9-C slice extends that
private fixture infrastructure with Sprite2D: only `spriteKey=1` is accepted, quads are expanded into transient
P2/UV2/ABGR geometry, and the current fixed fixture view order is View 0 clear, View 1 Opaque3D, View 2
Sprite2D, View 3 UI. That numbering is not a Pass Scheduler. `tina_sample_3d_infrastructure` runs the visible
3D fixture path for 300 frames with three cubes and one instance batch per frame; `tina_sample_2d_infrastructure_bgfx`
runs the visible 2D/UI fixture path for 300 frames with five sprites and two retained UI panels. Scene component
command buffers, Asset/Cooker integration, Texture/Sprite assets, TileMap/Box2D, Chinese text rendering,
generic Mesh/Material/PBR, the M10 product asset path, `tina_sample_2d`, and product samples remain later slices.
The current M8-B Windows MSVC Debug/Release Null gates both pass Core/Runtime 211/211, UI 115/115,
Runtime-to-UI 60/60, UI-to-Render 12/12, Scene 19/19, RenderScene 11/11, and both 300-frame Null and
2D-infrastructure samples. The current Windows Debug adapter recheck also passes GLFW 26/26, its 300-frame
platform sample, bgfx 16/16, and three consecutive 300-frame Desktop runs. The iconify regression keeps the
last positive logical extent while preserving a zero framebuffer for suspended-surface semantics. No new
visual screenshot was captured, so visible-image correctness still relies on the separately recorded D2 evidence;
Linux M8-B and visible Sprite rendering remain outstanding. The last full D2 Windows Debug/Release product gate remains the separately
recorded 207/92/53 matrix, UI-to-Render 12/12, bgfx 16/16, Null 300-frame run, and visible D3D11 Desktop
run. The latest
Linux Null evidence remains the 205/92/46/12 matrix and a 300-frame Null sample on GCC 13.4 and on
Clang 22.1.8 with libstdc++ 15.2 under ASan/UBSan/LSan with no diagnostic. The first GCC UI pass
exposed a `requires` name-visibility issue in the routed-pointer callback constraint; it is fixed.
The earlier Clang WSL2 Desktop run selected bgfx
Vulkan on llvmpipe, so it proves the Linux Vulkan/backend lifecycle, not hardware-GPU performance.

The current M9-A Windows MSVC Debug/Release recheck passes `tina_tests` 213/213 and
`tina_render_scene_tests` 22/22. `tina_sample_null`, `tina_sample_2d_infrastructure`, and
`tina_sample_3d_extraction` each run 300 frames and return zero; the 3D extraction sample records
4 submitted meshes, 3 visible meshes, 1 culled mesh, 2 instance batches, one aspect change, and
`liveResources=0`. Release also passes UI 115/115, Runtime-to-UI 60/60, UI-to-Render 12/12, and
Scene 19/19. The M9-C minimum direct gate is the bgfx graph:

```powershell
cmake --preset windows-msvc-vnext-bgfx
cmake --build --preset windows-vnext-bgfx-debug --target tina_render_bgfx_tests tina_sample_2d_infrastructure_bgfx
out\build\windows-msvc-vnext-bgfx\bin\Debug\tina_render_bgfx_tests.exe --gtest_color=yes
out\build\windows-msvc-vnext-bgfx\bin\Debug\tina_sample_2d_infrastructure_bgfx.exe --frames=300 --frame-delay-ms=0
```

The current M9-C Windows MSVC Debug/Release recheck passes the complete bgfx adapter suite 43/43 in each
configuration. In both configurations, `tina_sample_2d_infrastructure_bgfx` runs 300 frames with five fixture
sprites, two retained UI panels, one released UI root, a destroyed `EngineHost`, and a balanced Render resource
ledger. The Debug screenshot confirms sprite rotation, transparency, flip handling, and UI overlay after
Sprite2D. The known D3D11 debug-layer `RefCount=3` warning remains Debug-only and is absent from Release. This
is still fixture infrastructure, not the Asset/Texture/Sprite product path or the formal `tina_sample_2d`.
Linux M9-C remains unverified.

M10-A0 now provides the standalone `Tina::AssetFormat` foundation: distinct 16-byte `AssetId` and
`ContentHash` value types, fixed little-endian Cooked/Manifest wire schemas, deterministic object paths, and
borrowed parsers whose successful view publication allocates nothing, with hard-limit, checked-arithmetic, canonical-layout, padding, ordering, and
dependency validation. `tina_asset_format_tests` passes 14/14 in Windows MSVC Debug and Release. This slice does
not compute XXH3 or implement full DAG-cycle validation, file IO, the Asset registry/Handle/Lease lifecycle,
workers/uploads, cgltf, Cooker writes, or the formal 2D/3D asset product path; Cooked glTF remains incomplete.

M10-A1 now provides the standalone `Tina::Asset` `CatalogSnapshot`: a move-only immutable owning catalog built
transactionally from a validated `CookedManifestView` on an injected `std::pmr` resource, AssetId binary search,
resolved dependency entry indices, and iterative full DAG-cycle validation (`O(V + E)`, no recursive DFS).
Failed Create never publishes a partial snapshot. `tina_asset_tests` passes 17/17 in Windows MSVC Debug and
Release. This slice does not implement Handle/Lease, registry state machines, file IO, Task workers, GPU upload,
XXH3, cgltf, Cooker writes, or the formal product asset path; ADR 0016 remains Proposed.

M10-A2a provides the Core-private XXH3-128 v1 `ContentHash` digest (seed 0, little-endian 16-byte layout,
optional cooked payload verify, no public xxHash tokens). Windows MSVC Debug/Release `tina_tests` pass 218/218
and `tina_asset_format_tests` pass 16/16. Handle/Lease and Cooker remain out of scope.

M10-A2b provides bounded Core `readFile` and `loadCatalogSnapshotFromManifestFile`. Windows MSVC Debug/Release
`tina_tests` pass 223/223 and `tina_asset_tests` pass 19/19. Handle/Lease, async IO, and Cooker remain out of
scope.

M10-A2c–A2q provide cooked load, dependency order, batch load, package validate,
`openCatalogPackage`, the `tina_catalog_validate` CLI (including `--plan-loads`/`--load-assets`),
package summary diagnostics, load plans, one-shot package load, shared pipeline e2e fixtures,
`totalCookedFileBytes`, and `maxTotalCookedFileBytes` load budgets. Handle/Lease, async IO, and
Cooker remain out of scope.

M7-C1c-a adds fixed-capacity PMR pointer-policy/route-ancestry storage and a double-buffered
`UICommittedHitView`. Within one view, its effective-visible entries have unique, strictly increasing paint ordinals and
carry structure, layout, paint-order, and hit revisions; hit-only commits perform zero layout work, while a failed
`commitLayout()` publishes none of the structure/layout/hit candidates. M7-C1c-b1 adds the allocation-free
`queryPointerHit()` committed query, which scans front-to-back, returns route indices/revisions, and reports the
number of visited entries without triggering layout or dispatch. M7-C1c-b2 adds a synthetic `routePointerInput()`
path over the committed hit snapshot with fixed-capacity route path/listener storage, 48-byte fixed-inline
`noexcept` callbacks, generation-safe RAII listener tokens, owner-thread immediate reset, bounded off-thread
deferred reset, Capture -> Target -> Bubble dispatch, propagation stop/immediate stop, input-transition consume,
mutation-safe listener/node invalidation, and route/commit reentrancy guards. `UIContext` remains owner-thread
only and must not be destroyed from inside a route callback or callback cleanup. C1c-b3b adds the bounded
private producer, and C1c-b3c makes `EngineHost` lazily bind one private `UIContext` to the first primary
`WindowId`, run routing after Platform event dispatch and before `ActionMapper`, reject later window identity
loss/change, and destroy the Context before module shutdown. C1c-b3d1 moves the fixed-capacity UI settings into
the focused `UIContextCapacityConfig`, validates `EngineConfig::primaryWindowUICapacities` before any backend
factory runs, and adds a Runtime-private layout coordinator. After `IGameState::updateUI()` succeeds and before
render submission, the coordinator uses the primary window's logical extent to attempt at most one
`commitLayout()` per `PlatformFrameId`; a Headless frame with neither window nor Context is a successful no-op.
A failed commit blocks render, and the frame attempt remains consumed so the same mutations cannot be replayed.
Input routing still reads the previously committed hit snapshot. C1c-b3d2 adds backend-neutral
`initialPrimaryWindowMetrics()` seeding, explicit primary `UIContext` bind before `IGameState::onEnter()`,
startup structure/layout/hit publication before the state is committed, and move-only
`PrimaryWindowUIRootBuilder` / `PrimaryWindowUITreeUpdater` facades. Those facades are root-scoped,
owner-thread, phase-epoch-scoped, expire unconditionally when the callback exits, preserve the first
capability error as a sticky phase error, and never expose a raw `UIContext*`. The later compatible extension
adds `PrimaryWindowUITreeUpdater::addRoutedPointerListener()`: only its move-only token may survive the phase,
the token does not retain the Context or root, and a State resets it before releasing its `UIRootOwner` in
`onExit()`. Registration revalidates the root/subtree after moving the user callback and rolls back without
publishing a slot or high-water mark if reentrant callback code releases the root; destroying the Context from
callback move/cleanup is a lifecycle violation and terminates. C1c-b3e lets Move/Wheel/Button
routed Pointer listeners request button ownership; Runtime publishes only deduplicated buttons on `PrimaryPointerId`
still held in the final snapshot through bounded double-buffered claims, and `ActionMapper` cancels an active
Gameplay source or intercepts an unconsumed same-frame Down until the true Up. An EngineHost end-to-end test now
proves a Game SDK listener runs before ActionMapper and that a claim-only callback suppresses the same-frame Gameplay
action. The SolidFill paint/DisplayList/private bgfx panel path and primary Pointer Button default action now exist;
the current b4a slice also reuses clean-subtree Measure/Arrange work. This is still not a complete Widget pipeline:
Key/Gamepad/axis claim producers, text/glyph rendering, Label text, and Keyboard/Gamepad Button activation remain absent.
Persistent Pointer Capture, Focus/Modal, complete dirty-range pruning, nested clipping, IMM32 composition, production
gamepad input, Scene, Pass Scheduler, owning frame packets/pins, and submission tickets remain later slices.

The active design, verified status, and build instructions are maintained in
the [Chinese documentation](README_CN.md).
