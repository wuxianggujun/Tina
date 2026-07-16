# Tina Engine

Tina targets C++23 as its 2D/3D game-runtime language baseline and uses GLFW, bgfx, miniaudio, EnTT,
and a custom retained-mode UI. The current code keeps verified 2D/UI/3D paths alive while the vNext
architecture is designed as an incompatible, performance-first modular runtime. Migration will use
independently runnable vertical slices rather than an untestable single-shot rewrite.

Profiling uses Tina-owned trace points with optional Tracy; reproducible performance regression is
handled separately by `tina_bench`. SDL/SDL3 and CTest are not part of the target architecture.

The active design, verified status, and build instructions are maintained in
the [Chinese documentation](README_CN.md).
