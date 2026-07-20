#pragma once

#include <tina/asset_format/AssetFormat.hpp>
#include <tina/core/base/Types.hpp>
#include <tina/core/error/Result.hpp>
#include <tina/core/id/AssetId.hpp>

#include <span>
#include <vector>

namespace Tina::AssetFormat {

// TileMap cooked payload schema v1 (little-endian), single layer first slice.
// Layout:
//   u16 schemaVersion (=1)
//   u16 flags (=0 reserved)
//   u32 widthCells
//   u32 heightCells
//   f32 cellSizeMeters
//   u16 layerCount (=1 for v1)
//   u16 reserved (=0)
//   // layer 0 header (8B):
//   u16 layerId
//   u16 layerFlags
//   u32 tileCount  // must equal widthCells * heightCells
//   // tileCount * u16 localTileId (0 = empty)
// Dependency table: required Tileset.
namespace TileMapWire {
inline constexpr Core::u16 SchemaVersion = 1;
inline constexpr Core::u32 HeaderBytes = 20;
inline constexpr Core::u32 LayerHeaderBytes = 8;
inline constexpr Core::u32 MaxDimension = 1024;
inline constexpr Core::u16 EmptyTileId = 0;
} // namespace TileMapWire

struct TileMapPayloadDesc final {
    Core::u32 widthCells = 0;
    Core::u32 heightCells = 0;
    float cellSizeMeters = 1.0f;
    Core::u16 layerId = 0;
    Core::u16 layerFlags = 0;
    // Row-major cells; size must be widthCells * heightCells. 0 = empty.
    std::span<const Core::u16> tiles{};
    Core::AssetId tilesetId{}; // cooked dependency
};

struct TileMapPayloadView final {
    Core::u16 schemaVersion = 0;
    Core::u16 flags = 0;
    Core::u32 widthCells = 0;
    Core::u32 heightCells = 0;
    float cellSizeMeters = 1.0f;
    Core::u16 layerCount = 0;
    Core::u16 layerId = 0;
    Core::u16 layerFlags = 0;
    Core::u32 tileCount = 0;
    std::span<const std::byte> tilesBytes{};

    [[nodiscard]] std::optional<Core::u16> tileAt(Core::u32 x, Core::u32 y) const noexcept;
};

[[nodiscard]] Core::Result<std::vector<std::byte>> writeTileMapPayloadBytes(const TileMapPayloadDesc& desc);
[[nodiscard]] Core::Result<TileMapPayloadView> parseTileMapPayload(std::span<const std::byte> payload);

[[nodiscard]] Core::Result<std::vector<std::byte>>
writeCookedTileMapAsset(Core::AssetId tileMapId, const TileMapPayloadDesc& desc,
                        TargetPlatform platform = TargetPlatform::WindowsX64);

} // namespace Tina::AssetFormat
