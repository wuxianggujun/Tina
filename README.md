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
M7-B1 private WindowSurface handoff, and M7-B2 Desktop bootstrap plus real-GPU smoke are complete. The private `tina_platform_glfw` backend now
creates a `GLFW_NO_API` window, normalizes keyboard/pointer/focus/resize/close/committed UTF-8 text
into the same bounded `PlatformFrameView`, and hands a move-only window surface lease to the render
composition without exposing native or bgfx types. `Tina::Desktop::CreateEngine(config)` now privately
composes `SteadyClock + GLFW WindowSurface + DisabledTaskSystem + bgfx`, and `tina_sample_desktop`
defaults to 300 frames of deep-blue clear/present on the real render-backend path. Windows VS 2026/MSVC 19.50/
CMake 4.2.3 Debug and Release builds pass with direct GTest 183/183, GLFW 22/22, bgfx 11/11, and real
D3D11 Intel Iris Xe 300-frame smoke. Linux GCC 13.4 passes the same 183/22/11 direct-test matrix and a
300-frame Desktop run; Clang 22.1.8 passes it under ASan/UBSan/LSan. The Clang WSL2 run selected bgfx
Vulkan on llvmpipe, so it proves the Linux Vulkan/backend lifecycle, not hardware-GPU performance.
IMM32 composition, production gamepad input, Scene/UI, Pass
Scheduler, submission tickets, and retained UI remain later slices.

The active design, verified status, and build instructions are maintained in
the [Chinese documentation](README_CN.md).
