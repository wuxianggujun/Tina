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

The C++23 headless lifecycle kernel, M7-A platform/input kernel, and the first desktop adapter slice
are complete. The private `tina_platform_glfw` backend now creates a `GLFW_NO_API` window and
normalizes keyboard, pointer, focus, resize, close, and committed UTF-8 text into the same bounded
`PlatformFrameView`; `tina_sample_platform` combines it with NullRender. Native surface/bgfx remains
M7-B, while IMM32 composition, production gamepad input, and retained UI remain later slices.

The active design, verified status, and build instructions are maintained in
the [Chinese documentation](README_CN.md).
