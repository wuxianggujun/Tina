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
M7-C1b/M7-C1c-a/C1c-b1/C1c-b2/C1c-b3a/C1c-b3b/C1c-b3c/C1c-b3d1/b3d2/b3e retained tree, Flex-lite layout,
committed hit-snapshot, point-query, synthetic routed-pointer, private Runtime input-route,
startup primary-window UI seed, scoped Game SDK UI capability, held Pointer-button claim, and the
root-scoped Game SDK routed-pointer listener extension are complete. The private `tina_platform_glfw` backend now
creates a `GLFW_NO_API` window, normalizes keyboard/pointer/focus/resize/close/committed UTF-8 text
into the same bounded `PlatformFrameView`, and hands a move-only window surface lease to the render
composition without exposing native or bgfx types. `Tina::Desktop::CreateEngine(config)` now privately
composes `SteadyClock + GLFW WindowSurface + DisabledTaskSystem + bgfx`, and `tina_sample_desktop`
defaults to 300 frames on the real render-backend path. The latest listener-only Windows Debug gate on
VS 2026/MSVC 19.50/CMake 4.2.3 directly passes `tina_tests` 208/208, `tina_ui_tests` 95/95, and
`tina_runtime_ui_tests` 55/55, plus a 300-frame Null sample with one State exit and one Application shutdown.
The last full D2 Windows Debug/Release product gate remains the separately
recorded 207/92/53 matrix, UI-to-Render 12/12, bgfx 16/16, Null 300-frame run, and visible D3D11 Desktop
run; this listener slice did not re-run Release, Linux, GLFW, bgfx, or visible-product gates. The latest
Linux Null evidence remains the 205/92/46/12 matrix and a 300-frame Null sample on GCC 13.4 and on
Clang 22.1.8 with libstdc++ 15.2 under ASan/UBSan/LSan with no diagnostic. The first GCC UI pass
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
action. This is still not a complete Widget pipeline: the SolidFill paint/DisplayList/private bgfx panel path exists,
but Key/Gamepad/axis claim producers, text/glyph rendering, Label text, and Button default behavior remain absent.
Persistent Pointer Capture, Focus/Modal, dirty-subtree pruning, nested clipping, IMM32 composition, production
gamepad input, Scene, Pass Scheduler, owning frame packets/pins, and submission tickets remain later slices.

The active design, verified status, and build instructions are maintained in
the [Chinese documentation](README_CN.md).
