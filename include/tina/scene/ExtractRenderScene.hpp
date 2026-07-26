#pragma once

#include <tina/asset/AssetHandle.hpp>
#include <tina/core/error/Result.hpp>
#include <tina/render/Camera2DProjection.hpp>
#include <tina/render/RenderScene.hpp>
#include <tina/scene/World.hpp>

namespace Tina::Scene {

// Borrowed, allocation-free seam from a weak Sprite AssetHandle to the current
// backend-neutral render binding key. The caller keeps the callback and userData
// valid for one extractRenderSceneFromWorld() call; Scene retains neither. The
// callback is a phase-borrowed lookup: it validates handle identity, asset kind,
// and binding readiness without retaining pointers or references. Returning 0
// means unresolved.
struct Sprite2DBindingResolver final {
    using ResolveFn = Core::u32 (*)(void* userData, Asset::AssetHandle sprite) noexcept;

    void* userData = nullptr;
    ResolveFn resolve = nullptr;

    [[nodiscard]] constexpr explicit operator bool() const noexcept
    {
        return resolve != nullptr;
    }

    [[nodiscard]] Core::u32 operator()(Asset::AssetHandle sprite) const noexcept
    {
        return resolve == nullptr ? 0U : resolve(userData, sprite);
    }
};

// Surface framebuffer size for Camera2D projection resolve. Zero extent means
// Suspended surface: extract skips the World camera without treating it as a
// component configuration error (callers may still emit sprites or pure UI).
struct ExtractRenderSceneParams final {
    Render::Camera2DSurfaceViewport surfaceViewport{};
    Sprite2DBindingResolver spriteBindingResolver{};
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
//   spriteBindingResolver, then becomes addSprite2D using published world
//   position/scale and Z-axis rotation.
// - A missing resolver, invalid/stale/wrong-kind/unbound handle, or resolver
//   result 0 returns UnresolvedSprite. Hidden sprites are not resolved.
// - Each visible MeshRenderer3D with valid fixture mesh/material keys becomes
//   addMesh3D from WorldTransform pose/scale.
// - Optional SpriteOverrideFlags::UvRect copies uvRectOverride into
//   RenderSprite2DInput; otherwise UV defaults to full texture [0,1].
// - Does not require Runtime Phase Context World capability.
// - Does not retain AssetLease, AssetSystem, cooked payload, or GPU types.
[[nodiscard]] Core::Status extractRenderSceneFromWorld(
    World& world,
    Render::RenderSceneWriter& writer,
    ExtractRenderSceneParams params = {}) noexcept;

} // namespace Tina::Scene
