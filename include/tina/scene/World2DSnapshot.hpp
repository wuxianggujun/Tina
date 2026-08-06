#pragma once

#include <tina/asset/AssetHandle.hpp>
#include <tina/asset_format/World2DSnapshot.hpp>
#include <tina/core/error/Result.hpp>
#include <tina/core/id/AssetId.hpp>
#include <tina/scene/Entity.hpp>

#include <cstddef>
#include <functional>
#include <span>
#include <vector>

namespace Tina::Scene {

class World;

struct World2DSnapshotCaptureConfig final {
    // Required for a non-empty World. Must return a unique non-zero persistent ID.
    std::function<Core::u32(EntityId)> stableEntityId{};
    // Required when a serialized SpriteRenderer2D contains a weak asset handle.
    std::function<Core::AssetId(Asset::AssetHandle)> assetIdForHandle{};
    Core::u32 gameplaySchema = 0;
    Core::u32 gameplayVersion = 0;
    std::span<const std::byte> gameplayBytes{};
};

struct World2DSnapshotAssetResolver final {
    std::function<Asset::AssetHandle(Core::AssetId)> resolveSprite{};
    std::function<Asset::AssetHandle(Core::AssetId)> resolveTexture{};
};

struct World2DEntityBinding final {
    Core::u32 stableEntityId = 0;
    EntityId entity{};

    friend bool operator==(const World2DEntityBinding&, const World2DEntityBinding&) = default;
};

// Captures the current 2D World components into the unique schema-v1 snapshot.
// Runtime EntityId owner/index/generation bits never enter the byte stream.
// Entities carrying 3D components are rejected instead of being serialized lossy.
[[nodiscard]] Core::Result<std::vector<std::byte>>
captureWorld2DSnapshotBytes(const World& world, const World2DSnapshotCaptureConfig& config);

// Adds the snapshot entities to `world` in stable wire order. All resource
// resolution and capacity checks happen before mutation. Any later failure
// destroys every entity created by this call and preserves existing entities.
// The game-owned blob remains in snapshot.gameplayBytes for the caller to decode.
[[nodiscard]] Core::Result<std::vector<World2DEntityBinding>>
instantiateWorld2DSnapshot(World& world, const AssetFormat::World2DSnapshotView& snapshot,
                           const World2DSnapshotAssetResolver& assets = {});

} // namespace Tina::Scene
