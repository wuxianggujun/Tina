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

Profiling uses Tina-owned trace points with optional Tracy; reproducible performance regression is
handled separately by `tina_bench`. SDL/SDL3 and CTest are not part of the target architecture.

The C++23 headless lifecycle kernel, M7-A platform/input kernel, the first desktop adapter slice,
M7-B1 private WindowSurface handoff, M7-B2 Desktop bootstrap plus real-GPU smoke, and the
M7-C1b/M7-C1c-a/C1c-b1/C1c-b2/C1c-b3a/C1c-b3b/C1c-b3c/C1c-b3d1 retained tree, Flex-lite layout,
committed hit-snapshot, point-query, synthetic routed-pointer, and private Runtime input-route
foundations are complete. The private `tina_platform_glfw` backend now
creates a `GLFW_NO_API` window, normalizes keyboard/pointer/focus/resize/close/committed UTF-8 text
into the same bounded `PlatformFrameView`, and hands a move-only window surface lease to the render
composition without exposing native or bgfx types. `Tina::Desktop::CreateEngine(config)` now privately
composes `SteadyClock + GLFW WindowSurface + DisabledTaskSystem + bgfx`, and `tina_sample_desktop`
defaults to 300 frames of deep-blue clear/present on the real render-backend path. The current b3d1
Null graph passes direct Debug and Release gates on Windows VS 2026/MSVC 19.50/CMake 4.2.3:
`tina_tests` 189/189, `tina_ui_tests` 75/75, `tina_runtime_ui_tests` 29/29, and the Null sample for
300 frames. The b3d1 Windows bgfx graph also passes Debug and Release `tina_tests` 189/189,
`tina_ui_tests` 75/75, `tina_runtime_ui_tests` 29/29, GLFW 23/23, bgfx 11/11, and a 300-frame Desktop run on a real
D3D11 Intel Iris Xe; the Release process reports clean status. Linux GCC 13.4 passes the same
189/75/29 Null matrix and Null sample; Clang 22.1.8 with libstdc++ 15.2 passes it under
ASan/UBSan/LSan with no diagnostic. The first GCC UI pass
exposed a `requires` name-visibility issue in the routed-pointer callback constraint; it is fixed.
The earlier Clang WSL2 Desktop run selected bgfx
Vulkan on llvmpipe, so it proves the Linux Vulkan/backend lifecycle, not hardware-GPU performance.

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
Input routing still reads the previously committed hit snapshot. This is not a game-facing UI API: the Game SDK
still cannot create roots or widgets, no DisplayList is built, and the current empty Context therefore produces
canonical `None` consumption and claims with no visible UI. Startup primary-window metrics and root-scoped,
phase-epoch-scoped Game SDK UI capabilities are accepted by ADR 0021 but remain pending implementation. Persistent
Pointer Capture, Focus/Modal, Button default behavior, paint snapshots/DisplayList, dirty-subtree pruning, and nested clipping
are not implemented yet. IMM32 composition, production gamepad input, Scene, visible Runtime-integrated UI,
text/widgets, Pass Scheduler, and submission tickets remain later slices.

The active design, verified status, and build instructions are maintained in
the [Chinese documentation](README_CN.md).
