#pragma once

#include <tina/asset_format/AssetFormat.hpp>
#include <tina/core/base/Types.hpp>
#include <tina/core/error/Result.hpp>
#include <tina/core/id/AssetId.hpp>

#include <optional>
#include <span>
#include <vector>

namespace Tina::AssetFormat {

// TileMapChunk cooked payload schema v1 (little-endian).
//
// Header (48 bytes):
//   u16 schemaVersion (=1)
//   u16 flags (=0 reserved)
//   u8  parentTileMapId[16]
//   u32 layerId (non-zero)
//   u32 chunkX
//   u32 chunkY
//   u16 widthCells
//   u16 heightCells
//   u32 cellCount (= widthCells * heightCells)
//   u32 nonEmptyCount (= number of cells not equal to EmptyTileId)
//   u32 reserved (=0)
//   u16 cells[cellCount] (row-major)
namespace TileMapChunkWire {
inline constexpr Core::u16 SchemaVersion = 1;
inline constexpr Core::u32 HeaderBytes = 48;
inline constexpr Core::u16 MaxDimension = 64;
inline constexpr Core::u32 MaxCells = static_cast<Core::u32>(MaxDimension) * MaxDimension;
inline constexpr Core::u32 MaxChunkCoordinate = 1'048'575;
inline constexpr Core::u16 EmptyTileId = 0;
} // namespace TileMapChunkWire

struct TileMapChunkPayloadDesc final {
    Core::AssetId parentTileMapId{};
    Core::u32 layerId = 0;
    Core::u32 chunkX = 0;
    Core::u32 chunkY = 0;
    // Edge chunks use their actual dimensions rather than the nominal chunk dimensions.
    Core::u16 widthCells = 0;
    Core::u16 heightCells = 0;
    // Row-major and exactly widthCells * heightCells entries.
    std::span<const Core::u16> cells{};
};

struct TileMapChunkPayloadView final {
    Core::u16 schemaVersion = 0;
    Core::AssetId parentTileMapId{};
    Core::u32 layerId = 0;
    Core::u32 chunkX = 0;
    Core::u32 chunkY = 0;
    Core::u16 widthCells = 0;
    Core::u16 heightCells = 0;
    Core::u32 cellCount = 0;
    Core::u32 nonEmptyCount = 0;
    // Validated little-endian wire bytes. Use cellAt() for portable access.
    std::span<const std::byte> cellBytes{};

    [[nodiscard]] std::optional<Core::u16> cellAt(Core::u16 x, Core::u16 y) const noexcept;
};

// Stable current-schema identity for a chunk owned by one map/layer/coordinate.
// Layer reordering and unrelated chunk occupancy do not change the result.
[[nodiscard]] Core::Result<Core::AssetId>
deriveTileMapChunkAssetId(Core::AssetId parentTileMapId, Core::u32 stableLayerId,
                          Core::u32 chunkX, Core::u32 chunkY);

[[nodiscard]] Core::Result<std::vector<std::byte>>
writeTileMapChunkPayloadBytes(const TileMapChunkPayloadDesc& desc);

// Returned views borrow payload. The caller keeps the complete span alive and unchanged.
[[nodiscard]] Core::Result<TileMapChunkPayloadView>
parseTileMapChunkPayload(std::span<const std::byte> payload);

// The parent id is embedded in the payload rather than repeated as a cooked dependency. This
// keeps parent -> deferred chunk catalogs acyclic while preserving strict chunk ownership.
[[nodiscard]] Core::Result<std::vector<std::byte>>
writeCookedTileMapChunkAsset(Core::AssetId chunkAssetId, const TileMapChunkPayloadDesc& desc,
                            TargetPlatform platform = TargetPlatform::WindowsX64);

} // namespace Tina::AssetFormat
