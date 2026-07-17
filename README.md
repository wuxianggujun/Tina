# Tina Engine

Tina targets C++23 as its 2D/3D game-runtime language baseline and uses GLFW, a private bgfx backend,
miniaudio, EnTT, and a custom retained-mode UI. Game-facing APIs expose no third-party or bgfx types.
The current code keeps verified 2D/UI/3D paths alive while the vNext
architecture is designed as an incompatible, performance-first modular runtime. Migration will use
independently runnable vertical slices rather than an untestable single-shot rewrite.

The public lifecycle names are explicit: `IGameApplication` is the whole program entry point, while
`IGameState` represents a frame-driven menu, level, pause screen, or similar runtime state. The
ambiguous `IGame` name is not part of the vNext API.

Profiling uses Tina-owned trace points with optional Tracy; reproducible performance regression is
handled separately by `tina_bench`. SDL/SDL3 and CTest are not part of the target architecture.

The C++23 headless lifecycle kernel is complete. The next vertical slice is M7-A: explicit
`PlatformFrameView` and action-latch semantics, scripted/headless platform tests, and a private
GLFW `NO_API` window sample using NullRender. Native surface/bgfx and retained UI remain separate
M7-B–E slices.

The active design, verified status, and build instructions are maintained in
the [Chinese documentation](README_CN.md).
