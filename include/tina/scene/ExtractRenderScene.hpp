#pragma once

#include <tina/core/error/Result.hpp>
#include <tina/render/Camera2DProjection.hpp>
#include <tina/render/RenderScene.hpp>
#include <tina/scene/World.hpp>

namespace Tina::Scene {

// Surface framebuffer size for Camera2D projection resolve. Zero extent means
// Suspended surface: extract skips the World camera without treating it as a
// component configuration error (callers may still emit sprites or pure UI).
struct ExtractRenderSceneParams final {
    Render::Camera2DSurfaceViewport surfaceViewport{};
};

// Reads published World components into an existing phase-local
// RenderSceneWriter. Contract:
// - Calls updateWorldTransforms() first so WorldTransform is current.
// - Active Camera2D count 0: does not call setCamera2D (UI-only / no world view).
// - Active Camera2D count 1: resolves projection and setCamera2D once.
// - Active Camera2D count >1: structured MultipleActiveCameras failure.
// - Active PerspectiveCamera3D count 0: does not call setPerspectiveCamera.
// - Active PerspectiveCamera3D count 1: setPerspectiveCamera from WorldTransform pose.
// - Active PerspectiveCamera3D count >1: structured MultipleActiveCameras failure.
// - Camera2D and PerspectiveCamera3D are independent tracks (2D ortho vs 3D
//   perspective); both may be active in the same frame when the writer allows.
// - Each visible SpriteRenderer2D with valid spriteKey becomes addSprite2D
//   using published world position/scale and Z-axis rotation.
// - Each visible MeshRenderer3D with valid fixture mesh/material keys becomes
//   addMesh3D from WorldTransform pose/scale.
// - Optional SpriteOverrideFlags::UvRect copies uvRectOverride into
//   RenderSprite2DInput; otherwise UV defaults to full texture [0,1].
// - Does not require Runtime Phase Context World capability.
// - Does not resolve AssetSystem / Cooked Sprite or Mesh (fixture key path only).
[[nodiscard]] Core::Status extractRenderSceneFromWorld(
    World& world,
    Render::RenderSceneWriter& writer,
    ExtractRenderSceneParams params = {}) noexcept;

} // namespace Tina::Scene
