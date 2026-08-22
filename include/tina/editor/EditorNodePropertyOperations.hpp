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

namespace Tina::Editor {

// Node property edits never change a node's kind. An unset optional preserves
// each selected node's current value, which is how multi-selection Mixed fields
// are applied. A kind mismatch fails closed and a no-op publishes no revision.
struct World2DSpriteNodeProperties final {
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

struct World2DCameraNodeProperties final {
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

struct World2DPointLightNodeProperties final {
    std::optional<float> colorRed{};
    std::optional<float> colorGreen{};
    std::optional<float> colorBlue{};
    std::optional<float> colorAlpha{};
    std::optional<float> intensity{};
    std::optional<float> radiusMeters{};
    std::optional<float> sourceRadiusMeters{};
    std::optional<bool> active{};
};

struct World2DShadowOccluderNodeProperties final {
    std::optional<float> localStartX{};
    std::optional<float> localStartY{};
    std::optional<float> localEndX{};
    std::optional<float> localEndY{};
    std::optional<bool> active{};
};

struct World2DAnimatedSpriteNodeProperties final {
    std::optional<Core::AssetId> clipId{};
    std::optional<float> playbackSpeed{};
    std::optional<bool> autoPlay{};
};

struct World3DMeshNodeProperties final {
    std::optional<Core::AssetId> meshId{};
    std::optional<Core::AssetId> materialId{};
    std::optional<bool> visible{};
};

[[nodiscard]] Core::Result<EditorSceneOperationResult>
applyWorld2DSpriteNodeProperties(
    World2DAuthoringDocument& document,
    std::span<const Core::u32> stableEntityIds,
    const World2DSpriteNodeProperties& properties);

[[nodiscard]] Core::Result<EditorSceneOperationResult>
applyWorld2DCameraNodeProperties(
    World2DAuthoringDocument& document,
    std::span<const Core::u32> stableEntityIds,
    const World2DCameraNodeProperties& properties);

[[nodiscard]] Core::Result<EditorSceneOperationResult>
applyWorld2DPointLightNodeProperties(
    World2DAuthoringDocument& document,
    std::span<const Core::u32> stableEntityIds,
    const World2DPointLightNodeProperties& properties);

[[nodiscard]] Core::Result<EditorSceneOperationResult>
applyWorld2DShadowOccluderNodeProperties(
    World2DAuthoringDocument& document,
    std::span<const Core::u32> stableEntityIds,
    const World2DShadowOccluderNodeProperties& properties);

[[nodiscard]] Core::Result<EditorSceneOperationResult>
applyWorld2DAnimatedSpriteNodeProperties(
    World2DAuthoringDocument& document,
    std::span<const Core::u32> stableEntityIds,
    const World2DAnimatedSpriteNodeProperties& properties);

[[nodiscard]] Core::Result<EditorSceneOperationResult>
applyWorld3DMeshNodeProperties(
    World3DAuthoringDocument& document,
    std::span<const Core::u32> stableNodeIds,
    const World3DMeshNodeProperties& properties);

} // namespace Tina::Editor
