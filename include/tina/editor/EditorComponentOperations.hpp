#pragma once

#include <tina/core/base/Types.hpp>
#include <tina/core/error/Result.hpp>
#include <tina/core/id/AssetId.hpp>
#include <tina/editor/EditorSceneOperations.hpp>
#include <tina/editor/World2DAuthoringDocument.hpp>
#include <tina/editor/World3DAuthoringDocument.hpp>

#include <array>
#include <optional>
#include <span>
#include <string_view>

namespace Tina::Editor {

enum class World2DComponentKind : Core::u8 {
    Sprite = 0,
    Camera = 1,
    PointLight = 2,
    ShadowOccluder = 3,
    SpriteAnimation = 4,
};

inline constexpr Core::usize World2DComponentKindCount = 5;

struct EditorComponentInfo final {
    World2DComponentKind kind = World2DComponentKind::Sprite;
    std::string_view displayName{};
    bool removable = true;
};

// Static bounded registry describing every authorable World2D component. The
// intrinsic Transform is not listed because it cannot be added or removed.
[[nodiscard]] std::span<const EditorComponentInfo> world2DComponentRegistry() noexcept;

[[nodiscard]] bool hasWorld2DComponent(const AssetFormat::World2DEntityDesc& entity,
                                       World2DComponentKind kind) noexcept;

// Component commands follow the EditorSceneOperations contract: success
// publishes exactly one canonical document revision; failure preserves the
// document and both history branches unchanged.
//
// Add attaches a default-constructed component to every listed entity that
// lacks it. Remove detaches it from every listed entity that has it. Unknown
// stable IDs fail closed; a request that would change no entity fails closed
// (ComponentAlreadyPresent / ComponentNotFound) instead of publishing a no-op.
// Sprite and SpriteAnimation wire schemas require a non-zero AssetId, so those
// kinds fail closed unless componentAssetId resolves an asset (the sprite or
// the animation clip respectively). SpriteAnimation additionally requires a
// Sprite on the same entity; removing a Sprite cascades and also removes the
// entity's SpriteAnimation binding.
[[nodiscard]] Core::Result<EditorSceneOperationResult>
addWorld2DComponent(World2DAuthoringDocument& document,
                    std::span<const Core::u32> stableEntityIds,
                    World2DComponentKind kind,
                    Core::AssetId componentAssetId = {});

[[nodiscard]] Core::Result<EditorSceneOperationResult>
removeWorld2DComponent(World2DAuthoringDocument& document,
                       std::span<const Core::u32> stableEntityIds,
                       World2DComponentKind kind);

// Batch property edits mirror the Inspector transform Apply semantics: an
// unset optional means "leave the current per-entity value" (multi-select
// Mixed). Listed entities missing the component fail closed. An edit whose
// resolved values change nothing succeeds without publishing a revision.
struct World2DSpriteEditInput final {
    std::optional<Core::AssetId> spriteId{};
    std::optional<float> sizeX{};
    std::optional<float> sizeY{};
    std::optional<float> pivotX{};
    std::optional<float> pivotY{};
    std::optional<std::array<Core::u8, 4>> color{};
    std::optional<Core::i16> sortingLayer{};
    std::optional<Core::i32> orderInLayer{};
    std::optional<bool> flipX{};
    std::optional<bool> flipY{};
    std::optional<bool> visible{};
};

struct World2DCameraEditInput final {
    std::optional<AssetFormat::World2DCameraProjectionKind> projection{};
    std::optional<AssetFormat::World2DPixelSnapPolicy> pixelSnap{};
    std::optional<float> viewportX{};
    std::optional<float> viewportY{};
    std::optional<float> viewportWidth{};
    std::optional<float> viewportHeight{};
    std::optional<float> fixedWorldHeightMeters{};
    std::optional<float> referencePixelsPerMeter{};
    std::optional<Core::u32> referenceHeightPixels{};
    std::optional<bool> active{};
};

struct World2DPointLightEditInput final {
    std::optional<float> colorRed{};
    std::optional<float> colorGreen{};
    std::optional<float> colorBlue{};
    std::optional<float> colorAlpha{};
    std::optional<float> intensity{};
    std::optional<float> radiusMeters{};
    std::optional<float> sourceRadiusMeters{};
    std::optional<bool> active{};
};

struct World2DShadowOccluderEditInput final {
    std::optional<float> localStartX{};
    std::optional<float> localStartY{};
    std::optional<float> localEndX{};
    std::optional<float> localEndY{};
    std::optional<bool> active{};
};

struct World2DSpriteAnimationEditInput final {
    std::optional<Core::AssetId> clipId{};
    std::optional<float> playbackSpeed{};
    std::optional<bool> autoPlay{};
};

[[nodiscard]] Core::Result<EditorSceneOperationResult>
applyWorld2DSpriteEdit(World2DAuthoringDocument& document,
                       std::span<const Core::u32> stableEntityIds,
                       const World2DSpriteEditInput& input);

[[nodiscard]] Core::Result<EditorSceneOperationResult>
applyWorld2DCameraEdit(World2DAuthoringDocument& document,
                       std::span<const Core::u32> stableEntityIds,
                       const World2DCameraEditInput& input);

[[nodiscard]] Core::Result<EditorSceneOperationResult>
applyWorld2DPointLightEdit(World2DAuthoringDocument& document,
                           std::span<const Core::u32> stableEntityIds,
                           const World2DPointLightEditInput& input);

[[nodiscard]] Core::Result<EditorSceneOperationResult>
applyWorld2DShadowOccluderEdit(World2DAuthoringDocument& document,
                               std::span<const Core::u32> stableEntityIds,
                               const World2DShadowOccluderEditInput& input);

[[nodiscard]] Core::Result<EditorSceneOperationResult>
applyWorld2DSpriteAnimationEdit(World2DAuthoringDocument& document,
                                std::span<const Core::u32> stableEntityIds,
                                const World2DSpriteAnimationEditInput& input);

// World3D MeshRenderer is represented by paired non-zero mesh/material asset
// IDs on a prefab node. Add therefore requires both IDs; remove clears both.
[[nodiscard]] bool hasWorld3DMeshRenderer(const AssetFormat::PrefabNodeView& node) noexcept;

[[nodiscard]] Core::Result<EditorSceneOperationResult>
addWorld3DMeshRenderer(World3DAuthoringDocument& document,
                       std::span<const Core::u32> stableNodeIds,
                       Core::AssetId meshId,
                       Core::AssetId materialId);

[[nodiscard]] Core::Result<EditorSceneOperationResult>
removeWorld3DMeshRenderer(World3DAuthoringDocument& document,
                          std::span<const Core::u32> stableNodeIds);

struct World3DMeshRendererEditInput final {
    std::optional<Core::AssetId> meshId{};
    std::optional<Core::AssetId> materialId{};
    std::optional<bool> visible{};
};

[[nodiscard]] Core::Result<EditorSceneOperationResult>
applyWorld3DMeshRendererEdit(World3DAuthoringDocument& document,
                             std::span<const Core::u32> stableNodeIds,
                             const World3DMeshRendererEditInput& input);

} // namespace Tina::Editor
