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
    // Normalized sub-rect on the bound texture, which is how one spritesheet
    // feeds many nodes. Authoring any component sets the UvRect override flag;
    // the resulting rect must stay finite, within 0..1, and strictly ordered
    // (u0<u1, v0<v1), matching the runtime SpriteRenderer2D contract.
    std::optional<float> uvU0{};
    std::optional<float> uvV0{};
    std::optional<float> uvU1{};
    std::optional<float> uvV1{};
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

struct World2DPhysicsBodyNodeProperties final {
    std::optional<float> linearVelocityX{};
    std::optional<float> linearVelocityY{};
    std::optional<float> angularVelocityRadiansPerSecond{};
    std::optional<float> linearDamping{};
    std::optional<float> angularDamping{};
    std::optional<float> gravityScale{};
    std::optional<bool> enabled{};
    std::optional<bool> enableSleep{};
    std::optional<bool> initiallyAwake{};
    std::optional<bool> fixedRotation{};
    std::optional<bool> continuousCollision{};
};

struct World2DPhysicsShapeNodeProperties final {
    std::optional<AssetFormat::World2DPhysicsShapeKind> kind{};
    std::optional<float> halfExtentX{};
    std::optional<float> halfExtentY{};
    std::optional<float> radius{};
    std::optional<float> localCenterX{};
    std::optional<float> localCenterY{};
    std::optional<float> localAngleRadians{};
    std::optional<float> localPointAX{};
    std::optional<float> localPointAY{};
    std::optional<float> localPointBX{};
    std::optional<float> localPointBY{};
    std::optional<float> density{};
    std::optional<float> friction{};
    std::optional<float> restitution{};
    std::optional<bool> enabled{};
    std::optional<bool> sensor{};
    std::optional<bool> sensorEvents{};
    std::optional<bool> contactEvents{};
    std::optional<bool> hitEvents{};
};

// TileMap2D, FxEmitter2D, NavigationRegion2D and AudioPlayer2D each carry one
// required resource AssetId. Without this the asset could only be chosen at
// create time and never rebound.
struct World2DResourceNodeProperties final {
    std::optional<Core::AssetId> assetId{};
    std::optional<bool> active{};
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
applyWorld2DPhysicsBodyNodeProperties(
    World2DAuthoringDocument& document,
    std::span<const Core::u32> stableEntityIds,
    const World2DPhysicsBodyNodeProperties& properties);

[[nodiscard]] Core::Result<EditorSceneOperationResult>
applyWorld2DPhysicsShapeNodeProperties(
    World2DAuthoringDocument& document,
    std::span<const Core::u32> stableEntityIds,
    const World2DPhysicsShapeNodeProperties& properties);

[[nodiscard]] Core::Result<EditorSceneOperationResult>
applyWorld2DResourceNodeProperties(
    World2DAuthoringDocument& document,
    std::span<const Core::u32> stableEntityIds,
    const World2DResourceNodeProperties& properties);

[[nodiscard]] Core::Result<EditorSceneOperationResult>
applyWorld3DMeshNodeProperties(
    World3DAuthoringDocument& document,
    std::span<const Core::u32> stableNodeIds,
    const World3DMeshNodeProperties& properties);

} // namespace Tina::Editor
