#pragma once

#include <tina/asset/AssetFrameResourceResolver.hpp>
#include <tina/core/error/Result.hpp>
#include <tina/render/Camera2DProjection.hpp>
#include <tina/render/RenderScene.hpp>
#include <tina/scene/Sprite2DBindingResolver.hpp>
#include <tina/scene/World.hpp>

namespace Tina::Scene {

// Surface framebuffer size for Camera2D projection resolve. Zero extent means
// Suspended surface: extract skips the World camera without treating it as a
// component configuration error (callers may still emit sprites or pure UI).
struct ExtractRenderSceneParams final {
    Render::Camera2DSurfaceViewport surfaceViewport{};
    Sprite2DBindingResolver spriteBindingResolver{};
    Asset::AssetFrameResourceResolver mesh3DBindingResolver{};
    Asset::AssetFrameResourceResolver material3DBindingResolver{};
    float ambientLightScale = 0.18F;
    float ambientLight2DScale = 0.2F;
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
// - Each visible SpriteRenderer2D resolves its AssetHandle through
//   spriteBindingResolver into frameResources, then becomes addSprite2D using
//   published world position/scale and Z-axis rotation.
// - A missing resolver, invalid/stale/wrong-kind/unbound handle, or resolver
//   empty result returns UnresolvedSprite. Hidden sprites are not resolved.
// - Each visible MeshRenderer3D resolves its mesh/material AssetHandles through
//   the kind-specific mesh3DBindingResolver/material3DBindingResolver, then
//   becomes addMesh3D from WorldTransform pose/scale.
// - A missing resolver, invalid/stale/wrong-kind/unbound handle, or either
//   resolver empty result returns UnresolvedMesh. Hidden meshes are not resolved.
// - Active DirectionalLight3D components are sorted by stable entity identity,
//   transformed to world direction, and published as one bounded lighting snapshot.
// - Active PointLight2D components are sorted by stable entity identity and publish
//   world position/radius/color as one bounded, unshadowed Sprite2D lighting snapshot.
// - Optional SpriteOverrideFlags::UvRect copies uvRectOverride into
//   RenderSprite2DInput; otherwise UV defaults to full texture [0,1].
// - Does not require Runtime Phase Context World capability.
// - Does not retain AssetLease, AssetSystem, cooked payload, GPU types, or device keys.
[[nodiscard]] Core::Status extractRenderSceneFromWorld(
    World& world,
    Render::RenderSceneWriter& writer,
    Render::FrameResourceSink& frameResources,
    ExtractRenderSceneParams params = {}) noexcept;

} // namespace Tina::Scene
