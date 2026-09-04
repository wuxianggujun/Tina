#pragma once

#include <tina/asset/AssetFrameResourceResolver.hpp>
#include <tina/core/error/Result.hpp>
#include <tina/render/Camera2DProjection.hpp>
#include <tina/render/RenderScene.hpp>
#include <tina/scene/World.hpp>

namespace Tina::Scene {

// Borrowed, allocation-free seam from an entity with SkinnedMeshRenderer3D to
// the game-owned Animator3D CPU pose. The returned span must hold jointCount*16
// finite column-major floats (globalPose * inverseBind) and stays owned by the
// provider; extraction copies it into the committed RenderScene synchronously.
// An empty span fails extraction closed (UnresolvedSkinnedPose).
struct SkinnedPose3DProvider final {
    using ResolveFn = std::span<const float> (*)(void* userData, EntityId entity) noexcept;

    void* userData = nullptr;
    ResolveFn resolve = nullptr;

    [[nodiscard]] constexpr explicit operator bool() const noexcept { return resolve != nullptr; }

    [[nodiscard]] std::span<const float> operator()(EntityId entity) const noexcept
    {
        if (resolve == nullptr)
        {
            return {};
        }
        return resolve(userData, entity);
    }
};

// Surface framebuffer size for camera projection resolve. Zero extent means a
// suspended surface: extract skips camera-dependent light and mesh culling
// without treating the World camera as a component configuration error (callers
// may still emit scene items or pure UI).
struct ExtractRenderSceneParams final {
    Render::Camera2DSurfaceViewport surfaceViewport{};
    Asset::AssetFrameResourceResolver spriteBindingResolver{};
    Asset::AssetFrameResourceResolver normalTextureBindingResolver{};
    // Both are required when SpriteRenderer2D, MeshRenderer3D, or SkinnedMeshRenderer3D
    // carries a shader handle: the packet names a program and a value table separately,
    // and a program published without its uniform slot fails closed at submit. One
    // Mesh3D fragment binary covers rigid and skinned draws, so the same resolver pair
    // serves both 3D components.
    Asset::AssetFrameResourceResolver shaderBindingResolver{};
    Asset::AssetFrameResourceResolver shaderUniformBindingResolver{};
    // Keyed on the *mesh* handle, not a shader handle: a cooked StaticMesh/SkinnedMesh may name
    // its own default Mesh3D fragment stage, and only the mesh's cooked dependencies know which
    // Shader that is. Both are optional; leaving them unset means cooked defaults are ignored and
    // only the per-component handle above can replace the engine fragment.
    Asset::AssetFrameResourceResolver mesh3DDefaultShaderBindingResolver{};
    Asset::AssetFrameResourceResolver mesh3DDefaultShaderUniformBindingResolver{};
    Asset::AssetFrameResourceResolver mesh3DBindingResolver{};
    Asset::AssetFrameResourceResolver material3DBindingResolver{};
    Asset::AssetFrameResourceResolver skinnedMesh3DBindingResolver{};
    SkinnedPose3DProvider skinnedPose3DProvider{};
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
// - Each visible SpriteRenderer2D resolves its Sprite AssetHandle through
//   spriteBindingResolver into frameResources. When normalTexture has a value,
//   it must independently resolve through normalTextureBindingResolver to a
//   non-empty Texture2D ref before addSprite2D receives either binding.
// - When shader has a value, both shaderBindingResolver and
//   shaderUniformBindingResolver must resolve to non-empty refs of their own kind
//   before addSprite2D runs. An empty shader handle leaves both refs empty and
//   keeps the engine fragment.
// - A missing resolver, invalid/stale/wrong-kind/unbound handle, or resolver
//   empty result returns UnresolvedSprite. Hidden sprites resolve no handle.
// - Each visible MeshRenderer3D first validates and transforms its authored
//   local bounding sphere. With one resolved PerspectiveCamera3D on a non-zero
//   surface, sphere-vs-frustum culling occurs before resolving frame resources.
//   Camera-affecting meshes resolve mesh/material AssetHandles through the
//   kind-specific mesh3DBindingResolver/material3DBindingResolver, then become
//   addMesh3D from WorldTransform pose/scale. Without a resolved camera all
//   visible meshes preserve the existing fixed-capacity extraction behavior.
// - When MeshRenderer3D::shader has a value, the same shader/uniform resolver
//   pair as Sprite2D must resolve to non-empty refs before addMesh3D. Failures
//   return UnresolvedMesh, not UnresolvedSprite. An empty shader handle falls
//   back to the cooked mesh default below, and only then to the engine fragment.
// - With an empty component shader handle and both mesh3DDefaultShader*
//   resolvers set, the *mesh* handle resolves the fragment stage the cooked mesh
//   names for itself. Two empty refs mean the mesh names none and the engine
//   fragment stands. One empty and one not returns UnresolvedMesh, because a
//   program without its uniform slot fails closed at submit. Unset resolvers are
//   not a failure: callers that never cook a default keep the old behavior.
// - A missing resolver, invalid/stale/wrong-kind/unbound handle, or either
//   mesh/material resolver empty result returns UnresolvedMesh. Hidden and
//   frustum-culled meshes are not resolved, including their shader handle.
// - Each visible SkinnedMeshRenderer3D uses the same authored conservative
//   sphere and culling order, then resolves mesh/material through the
//   kind-specific skinnedMesh3DBindingResolver/material3DBindingResolver and its
//   CPU pose through skinnedPose3DProvider before addSkinnedMesh3D. A missing
//   provider or empty/malformed palette returns UnresolvedSkinnedPose; resolver
//   failures return UnresolvedMesh. Hidden and frustum-culled skinned meshes
//   resolve neither handles nor pose; extraction does not expand bounds for pose
//   deformation.
// - When SkinnedMeshRenderer3D::shader has a value, the same shader/uniform
//   resolver pair must resolve before addSkinnedMesh3D. One cooked Mesh3D
//   fragment binary links against both the rigid and skinned engine vertex
//   stages, so the component names the same Shader asset as MeshRenderer3D, and
//   the cooked-default fallback behaves identically for both components.
// - Active DirectionalLight3D components are sorted by stable entity identity,
//   transformed to world direction, and published as one bounded lighting snapshot.
//   At most one active component may own CascadedDirectionalShadow3D; extraction
//   resolves its directional-light index after sorting and deep-copies the settings.
// - Active PointLight3D components use WorldTransform position and are validated
//   before culling. With one active PerspectiveCamera3D on a non-zero surface,
//   exact sphere-vs-perspective-frustum culling runs before the eight-slot limit.
//   Without such a camera, including a suspended surface, all active point lights
//   retain the same fixed capacity.
// - Active SpotLight3D components use WorldTransform position and local -Z direction,
//   validate all active lights before culling, and reuse influence-sphere perspective
//   frustum culling before their independent eight-slot limit. No resolved perspective
//   camera, including a suspended surface, keeps all active spot lights capacity-bound.
//   Directional, point, and spot lights share one committed snapshot.
// - Active PointLight2D components are validated before culling. With one resolved
//   Camera2D on a non-zero surface, exact circle-vs-rotated-camera-rectangle culling
//   uses the same pixel-snapped center that RenderScene commits; boundary contact is
//   visible and only camera-affecting lights consume the eight committed slots.
//   The finite non-negative source radius is copied into the same snapshot, must
//   not exceed the influence radius, and does not expand the culling circle.
// - A ninth camera-affecting PointLight2D fails. With no resolved Camera2D, including
//   a suspended zero-extent surface, all active lights retain the same eight-slot cap.
// - ShadowOccluder2D components are not camera-culled because an off-camera segment
//   may still shadow a boundary light. Lights and segments are independently sorted
//   by stable entity identity and publish one bounded Sprite2D lighting snapshot.
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
