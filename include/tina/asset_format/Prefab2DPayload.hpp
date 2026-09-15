#pragma once

#include <tina/asset_format/AssetFormat.hpp>
#include <tina/asset_format/World2DSnapshot.hpp>
#include <tina/core/error/Result.hpp>
#include <tina/core/id/AssetId.hpp>

#include <optional>
#include <span>
#include <vector>

namespace Tina::AssetFormat {

// Prefab2D cooked payload schema v1: a current-schema World2D snapshot of a
// single-root subtree. The inner snapshot still carries World2D schema v9; this
// type version only changes if Prefab2D constraints or wrapping change.
namespace Prefab2DWire {
inline constexpr Core::u16 SchemaVersion = 1;
}

[[nodiscard]] constexpr std::optional<AssetKind> resourceAssetKindFor(
    World2DNodeKind kind) noexcept
{
    switch (kind)
    {
    case World2DNodeKind::TileMap2D:
        return AssetKind::TileMap;
    case World2DNodeKind::FxEmitter2D:
        return AssetKind::Fx2D;
    case World2DNodeKind::NavigationRegion2D:
        return AssetKind::NavigationGrid2D;
    case World2DNodeKind::AudioPlayer2D:
        return AssetKind::AudioClip;
    case World2DNodeKind::PrefabInstance2D:
        return AssetKind::Prefab2D;
    default:
        return std::nullopt;
    }
}

[[nodiscard]] Core::Status validatePrefab2DSnapshot(
    std::span<const World2DEntityDesc> entities) noexcept;

[[nodiscard]] Core::Result<std::vector<CookedAssetWriteDependency>>
collectPrefab2DDependencies(std::span<const World2DEntityDesc> entities);

// Writes a current-schema World2D snapshot with empty gameplay after Prefab2D
// validation. The caller wraps these bytes in a Cooked Prefab2D asset.
[[nodiscard]] Core::Result<std::vector<std::byte>> writePrefab2DPayloadBytes(
    std::span<const World2DEntityDesc> entities);

// Parses a Prefab2D payload. On failure, caller-owned entity storage is
// unchanged, matching parseWorld2DSnapshot.
[[nodiscard]] Core::Result<World2DSnapshotView> parsePrefab2DPayload(
    std::span<const std::byte> payload, std::vector<World2DEntityDesc>& entityStorage);

} // namespace Tina::AssetFormat
